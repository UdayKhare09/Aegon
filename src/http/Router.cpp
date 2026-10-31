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

    std::error_code ec;
    std::filesystem::path base_path = std::filesystem::weakly_canonical(base_dir, ec);
    std::string canonical_base = base_path.lexically_normal().string();
    while (canonical_base.size() > 1 && canonical_base.back() == '/') {
        canonical_base.pop_back();
    }

    auto cache = std::make_shared<std::unordered_map<std::string, CachedStaticFile>>();
    auto mtx = std::make_shared<std::shared_mutex>();

    auto handler = [canonical_base, options, cache, mtx](Context& ctx) {
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

        // 1. Fast lexical checks on both raw and URL-decoded path segments
        if (rel.find("..") != std::string_view::npos ||
            rel.find('\\') != std::string_view::npos ||
            rel.find('\0') != std::string_view::npos ||
            decoded_rel.find("..") != std::string::npos ||
            decoded_rel.find('\\') != std::string::npos ||
            decoded_rel.find('\0') != std::string::npos) {
            ctx.res().status(StatusCode::NotFound).text("Not Found");
            return;
        }

        std::string full_path = canonical_base + "/" + decoded_rel;

        auto accept_enc = ctx.req().header("accept-encoding");
        bool accept_br = options.precompressed && accept_enc && accept_enc->find("br") != std::string_view::npos;
        bool accept_gz = options.precompressed && accept_enc && (accept_enc->find("gzip") != std::string_view::npos || accept_enc->find("deflate") != std::string_view::npos);

        auto try_serve = [&](const std::string& file_path, std::string_view encoding, bool is_fallback) -> bool {
            int fd = -1;
            struct stat st{};
            time_t current_mtime = 0;
            long current_nsec = 0;

            if (options.cache_in_memory) {
                if (::stat(file_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                    return false;
                }
#if defined(__linux__)
                current_mtime = st.st_mtime;
                current_nsec = st.st_mtim.tv_nsec;
#elif defined(_WIN32)
                current_mtime = st.st_mtime;
                current_nsec = 0;
#else
                current_mtime = st.st_mtime;
                current_nsec = 0;
#endif
            } else {
#if defined(_WIN32)
                fd = ::_open(file_path.c_str(), _O_RDONLY | _O_BINARY);
                if (fd < 0) return false;
                struct _stat64 st_win{};
                if (::_fstat64(fd, &st_win) != 0 || !(st_win.st_mode & _S_IFREG)) {
                    ::_close(fd);
                    return false;
                }
                current_mtime = static_cast<time_t>(st_win.st_mtime);
                st.st_size = static_cast<off_t>(st_win.st_size);
#else
                fd = ::open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
                if (fd < 0) return false;
                if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
                    ::close(fd);
                    return false;
                }
                current_mtime = st.st_mtime;
                current_nsec = st.st_mtim.tv_nsec;
#endif
            }

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
                if (!options.cache_in_memory && st.st_size > 64 * 1024) {
                    if (fd >= 0) {
#if defined(_WIN32)
                        ::_close(fd);
#else
                        ::close(fd);
#endif
                        fd = -1;
                    }
                    entry = CachedStaticFile{
                        .mtime = current_mtime,
                        .mtime_nsec = current_nsec,
                        .size = static_cast<size_t>(st.st_size),
                        .etag = options.etag ? detail::generate_etag(current_mtime, current_nsec, st.st_size) : "",
                        .last_modified = options.last_modified ? detail::format_http_date(current_mtime) : "",
                        .content = "",
                        .content_type = std::string(content_type),
                        .content_encoding = std::string(encoding)
                    };
                } else {
                    if (fd < 0) {
#if defined(_WIN32)
                        fd = ::_open(file_path.c_str(), _O_RDONLY | _O_BINARY);
#else
                        fd = ::open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
#endif
                        if (fd < 0) return false;
                    }

                    std::string data;
                    data.resize(static_cast<size_t>(st.st_size));
#if defined(_WIN32)
                    int n = ::_read(fd, data.data(), static_cast<unsigned int>(data.size()));
                    ::_close(fd);
                    fd = -1;
                    if (n != static_cast<int>(data.size())) return false;
#else
                    ssize_t n = ::pread(fd, data.data(), data.size(), 0);
                    ::close(fd);
                    fd = -1;
                    if (n != static_cast<ssize_t>(data.size())) return false;
#endif

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

            if (entry.content.empty() && st.st_size > 0) {
                ctx.send_file(file_path, content_type);
            } else {
                ctx.res().body(entry.content);
            }
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
                    std::string fallback_path = canonical_base + "/" + options.fallback_file;
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
