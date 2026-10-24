#include "http/Router.h"
#include "core/OsSocket.h"
#include <sys/stat.h>
#include <fcntl.h>
#if defined(_WIN32)
#include <io.h>
#define O_BINARY _O_BINARY
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#else
#include <unistd.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#endif
#include <shared_mutex>
#include <mutex>
#include <memory>
#include <format>
#include <ctime>
#include <filesystem>

namespace aegon::http {

namespace detail {

inline std::string format_http_date(time_t t) {
    char buf[36];
    struct tm tm_buf;
#if defined(_WIN32)
    if (gmtime_s(&tm_buf, &t) == 0) {
#else
    if (gmtime_r(&t, &tm_buf)) {
#endif
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

struct CachedStaticFile {
    time_t mtime{0};
    long mtime_nsec{0};
    size_t size{0};
    std::string etag;
    std::string last_modified;
    std::string content;
    std::string content_type;
    std::string content_encoding;
};

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

    auto cache = std::make_shared<std::unordered_map<std::string, CachedStaticFile>>();
    auto mtx = std::make_shared<std::shared_mutex>();

    auto handler = [base_dir, options, cache, mtx](Context& ctx) {
        std::string_view rel;
        if (auto p = ctx.req().param("filepath")) {
            rel = *p;
        }

        std::string decoded_rel = Request::url_decode(rel);
        while (!decoded_rel.empty() && decoded_rel.front() == '/') {
            decoded_rel.erase(0, 1);
        }
        if (decoded_rel.empty()) {
            if (!options.index_file.empty()) {
                decoded_rel = options.index_file;
            } else {
                ctx.res().status(StatusCode::NotFound).text("Not Found");
                return;
            }
        }

        // 1. Lexical checks on both raw and URL-decoded path segments
        if (rel.find("..") != std::string_view::npos ||
            rel.find('\\') != std::string_view::npos ||
            rel.find('\0') != std::string_view::npos ||
            decoded_rel.find("..") != std::string::npos ||
            decoded_rel.find('\\') != std::string::npos ||
            decoded_rel.find('\0') != std::string::npos) {
            ctx.res().status(StatusCode::NotFound).text("Not Found");
            return;
        }

        // 2. Canonical filesystem boundaries: resolve symlinks and prevent directory breakouts
        std::error_code ec;
        std::filesystem::path base_path = std::filesystem::weakly_canonical(base_dir, ec);
        if (ec) {
            ctx.res().status(StatusCode::NotFound).text("Not Found");
            return;
        }

        std::filesystem::path target_path = std::filesystem::weakly_canonical(base_path / decoded_rel, ec);
        if (ec) {
            ctx.res().status(StatusCode::NotFound).text("Not Found");
            return;
        }

        auto base_native = base_path.lexically_normal().native();
        auto target_native = target_path.lexically_normal().native();
        if (!target_native.starts_with(base_native)) {
            ctx.res().status(StatusCode::NotFound).text("Not Found");
            return;
        }

        std::string full_path = target_path.string();

        auto accept_enc = ctx.req().header("accept-encoding");
        bool accept_br = options.precompressed && accept_enc && accept_enc->find("br") != std::string_view::npos;
        bool accept_gz = options.precompressed && accept_enc && (accept_enc->find("gzip") != std::string_view::npos || accept_enc->find("deflate") != std::string_view::npos);

        auto try_serve = [&](const std::string& file_path, std::string_view encoding, bool is_fallback) -> bool {
            struct stat st{};
            if (::stat(file_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                return false;
            }

#if defined(__linux__)
            time_t current_mtime = st.st_mtime;
            long current_nsec = st.st_mtim.tv_nsec;
#elif defined(__APPLE__)
            time_t current_mtime = st.st_mtime;
            long current_nsec = st.st_mtimespec.tv_nsec;
#elif defined(_WIN32)
            time_t current_mtime = st.st_mtime;
            long current_nsec = 0;
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (::GetFileAttributesExA(file_path.c_str(), GetFileExInfoStandard, &fad)) {
                ULARGE_INTEGER ull;
                ull.LowPart = fad.ftLastWriteTime.dwLowDateTime;
                ull.HighPart = fad.ftLastWriteTime.dwHighDateTime;
                current_mtime = static_cast<time_t>((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
                current_nsec = static_cast<long>(((ull.QuadPart - 116444736000000000ULL) % 10000000ULL) * 100);
            }
#else
            time_t current_mtime = st.st_mtime;
            long current_nsec = 0;
#endif

            std::string_view content_type = Response::infer_mime_type(file_path);
            CachedStaticFile entry;
            bool found_in_cache = false;

            if (options.cache_in_memory) {
                std::shared_lock lock(*mtx);
                auto it = cache->find(file_path);
                if (it != cache->end()) {
                    if (it->second.mtime == current_mtime && it->second.mtime_nsec == current_nsec) {
                        entry = it->second;
                        found_in_cache = true;
                    }
                }
            }

            if (!found_in_cache) {
#if defined(_WIN32)
                int fd = ::_open(file_path.c_str(), _O_RDONLY | _O_BINARY);
#else
                int fd = ::open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
#endif
                if (fd < 0) return false;

                std::string data;
                data.resize(static_cast<size_t>(st.st_size));
                size_t total_read = 0;
                while (total_read < data.size()) {
#if defined(_WIN32)
                    int n = ::_read(fd, data.data() + total_read, static_cast<unsigned int>(data.size() - total_read));
#else
                    ssize_t n = ::read(fd, data.data() + total_read, data.size() - total_read);
#endif
                    if (n <= 0) break;
                    total_read += static_cast<size_t>(n);
                }
#if defined(_WIN32)
                ::_close(fd);
#else
                ::close(fd);
#endif

                if (total_read != data.size()) return false;

                entry = CachedStaticFile{
                    .mtime = current_mtime,
                    .mtime_nsec = current_nsec,
                    .size = static_cast<size_t>(st.st_size),
                    .etag = options.etag ? detail::generate_etag(current_mtime, current_nsec, st.st_size) : "",
                    .last_modified = options.last_modified ? detail::format_http_date(current_mtime) : "",
                    .content = std::move(data),
                    .content_type = std::string(content_type),
                    .content_encoding = std::string(encoding)
                };

                if (options.cache_in_memory) {
                    std::unique_lock lock(*mtx);
                    (*cache)[file_path] = entry;
                }
            }

            // HTTP Conditional validation (If-None-Match / If-Modified-Since)
            bool is_not_modified = false;
            if (options.etag && !entry.etag.empty()) {
                if (auto inm = ctx.req().header("if-none-match"); inm) {
                    if (*inm == "*" || inm->find(entry.etag) != std::string_view::npos) {
                        is_not_modified = true;
                    }
                }
            }

            if (!is_not_modified && options.last_modified && !entry.last_modified.empty()) {
                if (auto ims = ctx.req().header("if-modified-since"); ims) {
                    if (*ims == entry.last_modified) {
                        is_not_modified = true;
                    }
                }
            }

            // 304 Not Modified
            if (is_not_modified) {
                ctx.res().status(StatusCode::NotModified);
                if (options.etag && !entry.etag.empty()) {
                    ctx.res().set_header_owned("ETag", entry.etag);
                }
                if (options.last_modified && !entry.last_modified.empty()) {
                    ctx.res().set_header_owned("Last-Modified", entry.last_modified);
                }
                if (options.cache_control) {
                    std::string cc = options.cache_control(rel, is_fallback);
                    if (!cc.empty()) ctx.res().set_header_owned("Cache-Control", std::move(cc));
                }
                return true;
            }

            // 200 OK
            ctx.res().status(StatusCode::Ok);
            ctx.res().header("Content-Type", content_type);
            if (!encoding.empty()) {
                ctx.res().header("Content-Encoding", encoding);
            }
            if (options.etag && !entry.etag.empty()) {
                ctx.res().set_header_owned("ETag", entry.etag);
            }
            if (options.last_modified && !entry.last_modified.empty()) {
                ctx.res().set_header_owned("Last-Modified", entry.last_modified);
            }
            if (options.cache_control) {
                std::string cc = options.cache_control(rel, is_fallback);
                if (!cc.empty()) ctx.res().set_header_owned("Cache-Control", std::move(cc));
            }

            ctx.res().body(entry.content);
            return true;
        };

        if (accept_br && try_serve(full_path + ".br", "br", false)) return;
        if (accept_gz && try_serve(full_path + ".gz", "gzip", false)) return;
        if (try_serve(full_path, "", false)) return;

        // SPA HTML5 History Mode Fallback
        if (options.spa && !options.fallback_file.empty()) {
            if (ctx.req().method() == Method::GET || ctx.req().method() == Method::HEAD) {
                bool is_asset_request = false;
                auto dot = rel.rfind('.');
                auto slash = rel.rfind('/');
                if (dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash)) {
                    std::string_view ext = rel.substr(dot);
                    if (ext != ".html" && ext != ".htm") {
                        is_asset_request = true;
                    }
                }

                bool accepts_html = false;
                if (auto accept = ctx.req().header("accept"); accept) {
                    if (accept->find("text/html") != std::string_view::npos) {
                        accepts_html = true;
                    }
                }

                if (!is_asset_request || accepts_html) {
                    std::string fallback_path = base_dir + "/" + options.fallback_file;
                    if (accept_br && try_serve(fallback_path + ".br", "br", true)) return;
                    if (accept_gz && try_serve(fallback_path + ".gz", "gzip", true)) return;
                    if (try_serve(fallback_path, "", true)) return;
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
