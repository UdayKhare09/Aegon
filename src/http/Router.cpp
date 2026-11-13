#include "http/Router.h"
#include "core/OsSocket.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#include "http/middleware/Compress.h"
#include <atomic>
#include <cerrno>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <thread>
#include <unordered_map>
#include <memory>
#include <format>
#include <ctime>
#include <filesystem>

namespace aegon::http {

namespace detail {

inline std::string format_http_date(time_t t) {
    char buf[36];
    struct tm tm_buf;
    if (gmtime_r(&t, &tm_buf)) {
        if (strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm_buf)) {
            return std::string(buf);
        }
    }
    return "";
}

inline std::string generate_etag(time_t mtime, long nsec, size_t size) {
    return std::format("\"{:#x}{:x}-{:#x}\"",
        static_cast<uint64_t>(mtime),
        static_cast<uint64_t>(nsec),
        static_cast<uint64_t>(size));
}

} // namespace detail

namespace detail {

// One file on disk, read once and shared by every response that serves it. Immutable: a
// change on disk produces a new blob, and responses still holding the old one keep it alive.
struct StaticBlob {
    std::string content;          // empty when `streamed` (too large to keep in memory)
    std::string path;             // on-disk path, for the streamed case
    std::string etag;
    std::string last_modified;
    std::string_view content_type;
    std::string_view encoding;    // "", "br" or "gzip"
    size_t size{0};
    time_t mtime{0};
    long mtime_nsec{0};
    bool streamed{false};
};

enum StaticVariantIndex : int { kIdentity = 0, kBrotli = 1, kGzip = 2 };

struct StaticCacheEntry {
    std::string rel;              // decoded path below the root
    uint64_t generation{0};
    // Per variant: the blob, or `missing` when the file is known not to exist.
    std::shared_ptr<const StaticBlob> blob[3];
    bool missing[3]{false, false, false};
    bool loaded[3]{false, false, false};
};

struct StaticWorkerCache {
    std::unordered_map<std::string, StaticCacheEntry, StringHash, StringEq> entries;
};

// Shared by every copy of one static_files() handler. `generation` is bumped by the
// directory watcher on any change under the root; workers compare it against their entry
// to know whether what they cached is still what the disk holds.
struct StaticFilesState {
    std::string base;
    StaticFilesOptions options;
    uint64_t id{0};
    std::atomic<uint64_t> generation{1};
    bool watching{false};
    int inotify_fd{-1};
    int stop_fd{-1};
    std::thread watcher;

    ~StaticFilesState() {
        if (stop_fd >= 0) {
            uint64_t one = 1;
            (void)::write(stop_fd, &one, sizeof(one));
        }
        if (watcher.joinable()) watcher.join();
        if (inotify_fd >= 0) ::close(inotify_fd);
        if (stop_fd >= 0) ::close(stop_fd);
    }

    // Starts an inotify watch on the root and every directory below it. Any event bumps
    // `generation`. Returns false (and leaves per-request stat revalidation on) if inotify
    // is unavailable.
    bool start_watching() {
        inotify_fd = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
        stop_fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (inotify_fd < 0 || stop_fd < 0) return false;
        constexpr uint32_t kMask = IN_CLOSE_WRITE | IN_MODIFY | IN_MOVED_TO | IN_MOVED_FROM |
                                   IN_CREATE | IN_DELETE | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF;
        auto add = [this](const std::string& dir) {
            return ::inotify_add_watch(inotify_fd, dir.c_str(), kMask) >= 0;
        };
        if (!add(base)) return false;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(base, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_directory(ec)) add(it->path().string());
        }
        watcher = std::thread([this, add] {
            alignas(struct inotify_event) char buf[16384];
            pollfd fds[2] = {{inotify_fd, POLLIN, 0}, {stop_fd, POLLIN, 0}};
            while (true) {
                if (::poll(fds, 2, -1) < 0) {
                    if (errno == EINTR) continue;
                    return;
                }
                if (fds[1].revents) return;
                ssize_t n = ::read(inotify_fd, buf, sizeof(buf));
                if (n <= 0) continue;
                for (char* p = buf; p < buf + n;) {
                    auto* ev = reinterpret_cast<struct inotify_event*>(p);
                    if ((ev->mask & IN_CREATE) && (ev->mask & IN_ISDIR) && ev->len > 0) {
                        // New subdirectory: watch it too. Its path is not tracked per wd,
                        // so re-scan the tree (directory creation is rare).
                        std::error_code e2;
                        for (auto it = std::filesystem::recursive_directory_iterator(base, e2);
                             !e2 && it != std::filesystem::recursive_directory_iterator(); it.increment(e2)) {
                            if (it->is_directory(e2)) add(it->path().string());
                        }
                    }
                    p += sizeof(struct inotify_event) + ev->len;
                }
                generation.fetch_add(1, std::memory_order_release);
            }
        });
        return true;
    }
};

inline std::atomic<uint64_t> g_static_state_ids{0};

// Per worker thread, per static_files() registration. No locks: each worker reads and
// fills its own cache. Keyed by the state's id (never reused), not its address.
inline StaticWorkerCache& static_worker_cache(const StaticFilesState& st) {
    thread_local uint64_t last_id = 0;
    thread_local StaticWorkerCache* last = nullptr;
    if (last_id == st.id && last) [[likely]] return *last;
    thread_local std::unordered_map<uint64_t, std::unique_ptr<StaticWorkerCache>> caches;
    auto& slot = caches[st.id];
    if (!slot) slot = std::make_unique<StaticWorkerCache>();
    last_id = st.id;
    last = slot.get();
    return *slot;
}

// Loads one variant from disk. Returns nullptr when it does not exist (or is not a file).
inline std::shared_ptr<const StaticBlob> load_static_blob(const StaticFilesOptions& options,
                                                          const std::string& path,
                                                          std::string_view content_type,
                                                          std::string_view encoding) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return nullptr;
    auto blob = std::make_shared<StaticBlob>();
    blob->path = path;
    blob->size = static_cast<size_t>(st.st_size);
    blob->mtime = st.st_mtime;
    blob->mtime_nsec = st.st_mtim.tv_nsec;
    blob->content_type = content_type;
    blob->encoding = encoding;
    if (options.etag) blob->etag = generate_etag(st.st_mtime, st.st_mtim.tv_nsec, blob->size);
    if (options.last_modified) blob->last_modified = format_http_date(st.st_mtime);
    if (!options.cache_in_memory || blob->size > options.max_cache_file_size) {
        blob->streamed = true;
        return blob;
    }
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return nullptr;
    blob->content.resize_and_overwrite(blob->size, [&](char* p, size_t n) {
        size_t got = 0;
        while (got < n) {
            ssize_t r = ::pread(fd, p + got, n - got, static_cast<off_t>(got));
            if (r <= 0) break;
            got += static_cast<size_t>(r);
        }
        return got;
    });
    ::close(fd);
    if (blob->content.size() != blob->size) return nullptr;  // changed under us: next request retries
    return blob;
}

// Without a directory watcher: is `blob` still what is on disk? One stat.
inline bool static_blob_current(const StaticBlob& blob) {
    struct stat st{};
    if (::stat(blob.path.c_str(), &st) != 0) return false;
    return st.st_mtime == blob.mtime && st.st_mtim.tv_nsec == blob.mtime_nsec &&
           static_cast<size_t>(st.st_size) == blob.size;
}

} // namespace detail

Router& Router::static_files(std::string_view prefix, std::string_view directory, StaticFilesOptions options) {
    std::string base_dir(directory);
    while (base_dir.size() > 1 && base_dir.back() == '/') {
        base_dir.pop_back();
    }

    std::string clean_prefix(prefix);
    if (!clean_prefix.empty() && !clean_prefix.starts_with('/')) {
        clean_prefix = "/" + clean_prefix;
    }
    while (clean_prefix.size() > 1 && clean_prefix.back() == '/') {
        clean_prefix.pop_back();
    }
    if (clean_prefix == "/") clean_prefix = "";

    std::error_code ec;
    std::filesystem::path base_path = std::filesystem::weakly_canonical(base_dir, ec);
    std::string canonical_base = base_path.lexically_normal().string();
    while (canonical_base.size() > 1 && canonical_base.back() == '/') {
        canonical_base.pop_back();
    }

    auto state = std::make_shared<detail::StaticFilesState>();
    state->base = canonical_base;
    state->options = options;
    state->id = ++detail::g_static_state_ids;
    if (options.cache_in_memory && options.watch_directory) {
        state->watching = state->start_watching();
    }

    auto handler = [state](Context& ctx) {
        const auto& options = state->options;
        std::string_view raw_rel;
        if (auto p = ctx.req().param("filepath")) {
            raw_rel = *p;
        }

        // Encodings the client takes, best first (q-values; br wins a tie). -1 ends the list.
        int order[4] = {detail::kIdentity, -1, -1, -1};
        if (options.precompressed) {
            if (auto ae = ctx.req().header("accept-encoding")) {
                auto acc = middleware::detail::parse_accept_encoding(*ae);
                const float qbr = acc.q(acc.br), qgz = acc.q(acc.gzip);
                int n = 0;
                if (qbr > 0 && qbr >= qgz) {
                    order[n++] = detail::kBrotli;
                    if (qgz > 0) order[n++] = detail::kGzip;
                } else if (qgz > 0) {
                    order[n++] = detail::kGzip;
                    if (qbr > 0) order[n++] = detail::kBrotli;
                }
                order[n++] = detail::kIdentity;
                order[n] = -1;
            }
        }

        auto& cache = detail::static_worker_cache(*state);
        const uint64_t gen = state->watching ? state->generation.load(std::memory_order_acquire) : 0;

        // The cache entry for `key` (request path below the prefix). Decoding and the path
        // checks run once, when the entry is created.
        auto lookup = [&](std::string_view key, std::string_view fallback_rel) -> detail::StaticCacheEntry* {
            auto it = cache.entries.find(key);
            if (it == cache.entries.end()) {
                std::string decoded = fallback_rel.empty() ? Request::url_decode(key) : std::string(fallback_rel);
                while (!decoded.empty() && decoded.front() == '/') decoded.erase(0, 1);
                if (decoded.empty()) {
                    if (options.index_file.empty()) return nullptr;
                    decoded = options.index_file;
                }
                if (key.find("..") != std::string_view::npos || key.find('\\') != std::string_view::npos ||
                    key.find('\0') != std::string_view::npos || decoded.find("..") != std::string::npos ||
                    decoded.find('\\') != std::string::npos || decoded.find('\0') != std::string::npos) {
                    return nullptr;
                }
                // Bounded: probes for paths that do not exist must not grow it forever.
                if (cache.entries.size() >= 65536) cache.entries.clear();
                it = cache.entries.emplace(std::string(key), detail::StaticCacheEntry{}).first;
                it->second.rel = std::move(decoded);
                it->second.generation = gen;
            }
            auto& e = it->second;
            if (state->watching && e.generation != gen) {
                for (int i = 0; i < 3; ++i) {
                    e.blob[i].reset();
                    e.missing[i] = false;
                    e.loaded[i] = false;
                }
                e.generation = gen;
            }
            return &e;
        };

        // The blob for variant `v` of `e`, loading (or re-validating) it as needed.
        auto variant = [&](detail::StaticCacheEntry& e, int v) -> std::shared_ptr<const detail::StaticBlob> {
            if (e.loaded[v]) {
                if (e.missing[v]) {
                    if (state->watching) return nullptr;
                    // No watcher: a missing variant may have appeared; check again.
                } else if (state->watching || detail::static_blob_current(*e.blob[v])) {
                    return e.blob[v];
                }
            }
            static constexpr std::string_view kSuffix[3] = {"", ".br", ".gz"};
            static constexpr std::string_view kEncoding[3] = {"", "br", "gzip"};
            std::string path;
            path.reserve(state->base.size() + 1 + e.rel.size() + 3);
            path.append(state->base).append("/").append(e.rel).append(kSuffix[v]);
            auto blob = detail::load_static_blob(options, path,
                                                 Response::infer_mime_type(e.rel), kEncoding[v]);
            e.loaded[v] = true;
            e.missing[v] = (blob == nullptr);
            e.blob[v] = blob;
            return blob;
        };

        // Serves `key` if it exists. `is_fallback` is passed to the cache_control callback.
        auto serve = [&](std::string_view key, std::string_view fallback_rel, bool is_fallback) -> bool {
            auto* e = lookup(key, fallback_rel);
            if (!e) return false;
            std::shared_ptr<const detail::StaticBlob> blob;
            for (int i = 0; order[i] >= 0 && !blob; ++i) {
                blob = variant(*e, order[i]);
            }
            if (!blob) return false;
            const bool has_variants = options.precompressed &&
                ((e->loaded[detail::kBrotli] && !e->missing[detail::kBrotli]) ||
                 (e->loaded[detail::kGzip] && !e->missing[detail::kGzip]) || !blob->encoding.empty());

            auto& res = ctx.res();
            // Header values below are views into the blob; retaining it keeps them valid.
            res.retain(blob);

            bool not_modified = false;
            if (options.etag && !blob->etag.empty()) {
                if (auto inm = ctx.req().header("if-none-match")) {
                    not_modified = (*inm == "*" || inm->find(blob->etag) != std::string_view::npos);
                }
            }
            if (!not_modified && options.last_modified && !blob->last_modified.empty()) {
                if (auto ims = ctx.req().header("if-modified-since")) {
                    not_modified = (*ims == blob->last_modified);
                }
            }
            if (not_modified) {
                res.status(StatusCode::NotModified);
            } else {
                res.status(StatusCode::Ok);
                res.header("Content-Type", blob->content_type);
                if (!blob->encoding.empty()) res.header("Content-Encoding", blob->encoding);
            }
            if (has_variants) res.header("Vary", "Accept-Encoding");
            if (options.etag && !blob->etag.empty()) res.header("ETag", blob->etag);
            if (options.last_modified && !blob->last_modified.empty()) res.header("Last-Modified", blob->last_modified);
            if (options.cache_control) {
                std::string cc = options.cache_control(e->rel, is_fallback);
                if (!cc.empty()) res.set_header_owned("Cache-Control", std::move(cc));
            }
            if (not_modified) return true;

            if (blob->streamed) {
                ctx.send_file(blob->path, blob->content_type);
                res.retain(blob);
            } else {
                res.body_shared(blob, blob->content);
            }
            return true;
        };

        if (serve(raw_rel, {}, false)) return;

        // SPA HTML5 History Mode Fallback
        if (options.spa && !options.fallback_file.empty()) {
            if (ctx.req().method() == Method::GET || ctx.req().method() == Method::HEAD) {
                std::string decoded = Request::url_decode(raw_rel);
                bool is_asset_request = false;
                auto dot = decoded.rfind('.');
                auto slash = decoded.rfind('/');
                if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
                    std::string_view ext = std::string_view(decoded).substr(dot);
                    if (ext != ".html" && ext != ".htm") {
                        is_asset_request = true;
                    }
                }
                bool accepts_html = false;
                if (auto accept = ctx.req().header("accept"); accept) {
                    accepts_html = accept->find("text/html") != std::string_view::npos;
                }
                if (!is_asset_request || accepts_html) {
                    // Cached under a key that cannot collide with a request path.
                    std::string key = "\x01spa-fallback/" + options.fallback_file;
                    if (serve(key, options.fallback_file, true)) return;
                }
            }
        }

        ctx.res().status(StatusCode::NotFound).text("Not Found");
    };

    std::string wildcard_pattern = clean_prefix + "/*filepath";
    add_route(Method::GET, wildcard_pattern, handler);
    add_route(Method::HEAD, wildcard_pattern, handler);

    if (!clean_prefix.empty()) {
        add_route(Method::GET, clean_prefix, handler);
        add_route(Method::HEAD, clean_prefix, handler);
        add_route(Method::GET, clean_prefix + "/", handler);
        add_route(Method::HEAD, clean_prefix + "/", handler);
    } else {
        add_route(Method::GET, "/", handler);
        add_route(Method::HEAD, "/", handler);
    }

    return *this;
}

} // namespace aegon::http
