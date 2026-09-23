#pragma once

#include "http/Protocol.h"
#include "http/Context.h"
#include "http/RadixTree.h"
#include "http/RouteGroup.h"
#include "http/Middleware.h"
#include "http/websocket/WebSocket.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <type_traits>
#include <concepts>
#include <functional>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace aegon::http {

struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
    size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

struct StringEq {
    using is_transparent = void;
    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return lhs == rhs;
    }
};

class Router {
    struct StaticRouteEntry {
        std::array<Handler, 9> handlers{};
        std::array<bool, 9> has_handler{};
        bool has_any_handler{false};
    };

    static std::string normalize_route_path(std::string_view p) noexcept {
        if (p.empty()) return "/";
        if (p.size() > 1 && p.ends_with('/')) {
            p.remove_suffix(1);
        }
        return std::string(p);
    }

public:
    Router() = default;
    ~Router() = default;

    Router(Router&&) noexcept = default;
    Router& operator=(Router&&) noexcept = default;
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;

    /**
     * @brief Creates a hierarchical RouteGroup with a path prefix.
     */
    [[nodiscard]] RouteGroup group(std::string_view prefix) {
        return RouteGroup(*this, std::string(prefix));
    }

    /**
     * @brief Wraps both synchronous void(Context&) and asynchronous Task<void>(Context&) callables.
     */
    template <typename F>
    static Handler make_handler(F&& f) {
        if constexpr (std::is_invocable_r_v<core::Task<void>, F, Context&>) {
            return std::forward<F>(f);
        } else if constexpr (std::is_invocable_r_v<void, F, Context&>) {
            return [func = std::forward<F>(f)](Context& ctx) -> core::Task<void> {
                func(ctx);
                co_return;
            };
        } else {
            static_assert(sizeof(F) == 0, "Handler must be callable as (Context&) returning void or Task<void>");
        }
    }

    /**
     * @brief Appends global middleware applied to all matching routes.
     */
    template <typename F>
    Router& use(F&& mw) {
        global_middleware_.push_back(make_middleware(std::forward<F>(mw)));
        return *this;
    }

    [[nodiscard]] const std::vector<MiddlewareFn>& global_middleware() const noexcept {
        return global_middleware_;
    }

    template <typename F>
    Router& add_route(Method method, std::string_view pattern,
                      const std::vector<MiddlewareFn>& group_mw,
                      const std::vector<MiddlewareFn>& route_mw,
                      F&& handler) {
        Handler raw = make_handler(std::forward<F>(handler));
        Handler h;

        if (group_mw.empty() && route_mw.empty()) {
            h = std::move(raw);
        } else {
            std::vector<MiddlewareFn> chain;
            chain.reserve(group_mw.size() + route_mw.size());
            chain.insert(chain.end(), group_mw.begin(), group_mw.end());
            chain.insert(chain.end(), route_mw.begin(), route_mw.end());

            h = [local_chain = std::move(chain), target = std::move(raw)](Context& ctx) -> core::Task<void> {
                co_await run_chain({}, local_chain, 0, target, ctx);
            };
        }

        // Fast-path Tier 1: Check if pattern is purely static (no :param or *wildcard)
        if (pattern.find_first_of(":*") == std::string_view::npos) {
            std::string norm = normalize_route_path(pattern);
            auto& entry = static_routes_[norm];
            size_t idx = static_cast<size_t>(method);
            if (idx < entry.handlers.size()) {
                entry.handlers[idx] = h;
                entry.has_handler[idx] = true;
                entry.has_any_handler = true;
            }
        }

        // Tier 2: Radix Tree registration
        tree_.insert(method, pattern, std::move(h));
        return *this;
    }

    template <typename F>
    Router& add_route(Method method, std::string_view pattern, F&& handler) {
        return add_route(method, pattern, {}, {}, std::forward<F>(handler));
    }

    template <typename F>
    Router& add_route(Method method, std::string_view pattern, std::vector<MiddlewareFn> route_mw, F&& handler) {
        return add_route(method, pattern, {}, std::move(route_mw), std::forward<F>(handler));
    }

    template <typename F>
    Router& get(std::string_view pattern, F&& handler) {
        return add_route(Method::GET, pattern, {}, {}, std::forward<F>(handler));
    }

    template <typename F>
    Router& get(std::string_view pattern, std::vector<MiddlewareFn> route_mw, F&& handler) {
        return add_route(Method::GET, pattern, {}, std::move(route_mw), std::forward<F>(handler));
    }

    template <typename F>
    Router& post(std::string_view pattern, F&& handler) {
        return add_route(Method::POST, pattern, {}, {}, std::forward<F>(handler));
    }

    template <typename F>
    Router& post(std::string_view pattern, std::vector<MiddlewareFn> route_mw, F&& handler) {
        return add_route(Method::POST, pattern, {}, std::move(route_mw), std::forward<F>(handler));
    }

    template <typename F>
    Router& put(std::string_view pattern, F&& handler) {
        return add_route(Method::PUT, pattern, {}, {}, std::forward<F>(handler));
    }

    template <typename F>
    Router& put(std::string_view pattern, std::vector<MiddlewareFn> route_mw, F&& handler) {
        return add_route(Method::PUT, pattern, {}, std::move(route_mw), std::forward<F>(handler));
    }

    template <typename F>
    Router& del(std::string_view pattern, F&& handler) {
        return add_route(Method::DELETE, pattern, {}, {}, std::forward<F>(handler));
    }

    template <typename F>
    Router& del(std::string_view pattern, std::vector<MiddlewareFn> route_mw, F&& handler) {
        return add_route(Method::DELETE, pattern, {}, std::move(route_mw), std::forward<F>(handler));
    }

    template <typename F>
    Router& patch(std::string_view pattern, F&& handler) {
        return add_route(Method::PATCH, pattern, {}, {}, std::forward<F>(handler));
    }

    template <typename F>
    Router& patch(std::string_view pattern, std::vector<MiddlewareFn> route_mw, F&& handler) {
        return add_route(Method::PATCH, pattern, {}, std::move(route_mw), std::forward<F>(handler));
    }

    template <typename F>
    Router& all(std::string_view pattern,
                const std::vector<MiddlewareFn>& group_mw,
                const std::vector<MiddlewareFn>& route_mw,
                F&& handler) {
        static constexpr Method all_methods[] = {
            Method::GET, Method::POST, Method::PUT, Method::DELETE,
            Method::PATCH, Method::HEAD, Method::OPTIONS
        };
        Handler h = make_handler(std::forward<F>(handler));
        for (Method m : all_methods) {
            add_route(m, pattern, group_mw, route_mw, h);
        }
        return *this;
    }

    template <typename F>
    Router& all(std::string_view pattern, F&& handler) {
        return all(pattern, {}, {}, std::forward<F>(handler));
    }

    template <typename F>
    Router& all(std::string_view pattern, std::vector<MiddlewareFn> route_mw, F&& handler) {
        return all(pattern, {}, std::move(route_mw), std::forward<F>(handler));
    }

    template <typename ClusterT, typename OptionsT>
    Router& proxy(std::string_view pattern,
                  ClusterT cluster,
                  OptionsT options,
                  std::vector<MiddlewareFn> middlewares = {}) {
        return all(pattern, {}, std::move(middlewares),
                   make_proxy_handler(std::move(cluster), std::move(options)));
    }

    /**
     * @brief Serves static files rooted at `directory` under `prefix`.
     *
     * Supports precompressed (.br, .gz) sidecar negotiation and disk-following
     * in-memory caching with mtime revalidation.
     */
    Router& static_files(std::string_view prefix, std::string_view directory, StaticFilesOptions options = {});

    struct WebSocketRouteEntry {
        websocket::WebSocketHandler handler{nullptr};
        websocket::WebSocketEchoHandler echo_handler{nullptr};
    };

    /**
     * @brief Registers an RFC 6455 WebSocket route.
     * Also registers an HTTP GET fallback returning 426 Upgrade Required for non-upgrade requests.
     */
    Router& ws(std::string_view pattern, websocket::WebSocketHandler handler = nullptr) {
        std::string norm = normalize_route_path(pattern);
        ws_routes_[norm] = WebSocketRouteEntry{.handler = std::move(handler), .echo_handler = nullptr};
        get(pattern, [](Context& ctx) {
            ctx.res().status(StatusCode::UpgradeRequired)
                     .header("Upgrade", "websocket")
                     .header("Connection", "Upgrade")
                     .text("426 Upgrade Required: WebSocket connection expected\n");
        });
        return *this;
    }

    Router& ws(std::string_view pattern, websocket::WebSocketEchoHandler echo_handler) {
        std::string norm = normalize_route_path(pattern);
        ws_routes_[norm] = WebSocketRouteEntry{.handler = nullptr, .echo_handler = std::move(echo_handler)};
        get(pattern, [](Context& ctx) {
            ctx.res().status(StatusCode::UpgradeRequired)
                     .header("Upgrade", "websocket")
                     .header("Connection", "Upgrade")
                     .text("426 Upgrade Required: WebSocket connection expected\n");
        });
        return *this;
    }

    Router& ws_echo(std::string_view pattern) {
        return ws(pattern);
    }

    [[nodiscard]] const WebSocketRouteEntry* find_ws(std::string_view path) const noexcept {
        if (ws_routes_.empty()) return nullptr;
        std::string_view norm_path = path;
        if (norm_path.empty()) {
            norm_path = "/";
        } else if (norm_path.size() > 1 && norm_path.ends_with('/')) {
            norm_path.remove_suffix(1);
        }
        auto it = ws_routes_.find(norm_path);
        if (it != ws_routes_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    [[nodiscard]] bool has_ws(std::string_view path) const noexcept {
        return find_ws(path) != nullptr;
    }

    using MatchResult = RadixTree::MatchResult;

    [[nodiscard]] MatchResult match(Request& req) const {
        // Fast-Path Tier 1: O(1) exact static route lookup with 0 heap allocations
        std::string_view req_path = req.path();
        if (req_path.empty()) {
            req_path = "/";
        } else if (req_path.size() > 1 && req_path.ends_with('/')) {
            req_path.remove_suffix(1);
        }

        auto it = static_routes_.find(req_path);
        if (it != static_routes_.end()) {
            size_t midx = static_cast<size_t>(req.method());
            if (midx < it->second.handlers.size() && it->second.has_handler[midx]) {
                return MatchResult{
                    .handler = &it->second.handlers[midx],
                    .route_found = true,
                    .method_not_allowed = false
                };
            }
            // Path matched statically, but method was not registered
            return MatchResult{
                .handler = nullptr,
                .route_found = true,
                .method_not_allowed = true
            };
        }

        // Tier 2: Dynamic / parameterized / wildcard Radix Tree resolution
        return tree_.match(req);
    }

    [[nodiscard]] size_t static_route_count() const noexcept {
        return static_routes_.size();
    }

    using ErrorHandler = std::function<core::Task<void>(Context&, std::exception_ptr)>;
    using FallbackHandler = Handler;

    template <typename F>
    Router& set_error_handler(F&& handler) {
        if constexpr (std::is_invocable_r_v<core::Task<void>, F, Context&, std::exception_ptr>) {
            error_handler_ = std::forward<F>(handler);
        } else if constexpr (std::is_invocable_r_v<void, F, Context&, std::exception_ptr>) {
            error_handler_ = [func = std::forward<F>(handler)](Context& ctx, std::exception_ptr ex) -> core::Task<void> {
                func(ctx, ex);
                co_return;
            };
        } else {
            static_assert(sizeof(F) == 0, "ErrorHandler must be callable as (Context&, std::exception_ptr) returning void or Task<void>");
        }
        return *this;
    }

    template <typename F>
    Router& set_not_found_handler(F&& handler) {
        not_found_handler_ = make_handler(std::forward<F>(handler));
        return *this;
    }

    template <typename F>
    Router& set_method_not_allowed_handler(F&& handler) {
        method_not_allowed_handler_ = make_handler(std::forward<F>(handler));
        return *this;
    }

    [[nodiscard]] const ErrorHandler& error_handler() const noexcept { return error_handler_; }
    [[nodiscard]] const FallbackHandler& not_found_handler() const noexcept { return not_found_handler_; }
    [[nodiscard]] const FallbackHandler& method_not_allowed_handler() const noexcept { return method_not_allowed_handler_; }

    /**
     * @brief Dispatches an inbound request to its matched route handler within a global exception boundary.
     *
     * Catches unhandled exceptions and invokes error_handler or returns standard RFC 7807 500 JSON.
     * Invokes custom or standard RFC 7807 handlers for 404 (Not Found) and 405 (Method Not Allowed).
     */
    core::Task<void> dispatch(Request& req, Response& res, const ServiceRegistry* services) const {
        auto match_res = match(req);
        Context ctx(req, res, services);

        // Fast-path hot path: Exact route found, no global middleware
        if (__builtin_expect(match_res.route_found && match_res.handler != nullptr && global_middleware_.empty(), 1)) {
            std::exception_ptr ex{nullptr};
            try {
                co_await (*match_res.handler)(ctx);
            } catch (...) {
                ex = std::current_exception();
            }
            if (__builtin_expect(!ex, 1)) {
                co_return;
            }
            if (error_handler_) {
                try {
                    co_await error_handler_(ctx, ex);
                } catch (...) {
                    ctx.problem(StatusCode::InternalServerError, "Internal Server Error", "Unknown error in custom error handler");
                }
            } else {
                std::string detail = "An internal server error occurred.";
                try {
                    std::rethrow_exception(ex);
                } catch (const std::exception& e) {
                    detail = e.what();
                } catch (...) {}
                ctx.problem(StatusCode::InternalServerError, "Internal Server Error", detail);
            }
            co_return;
        }

        auto execute_route = [&]() -> core::Task<void> {
            if (match_res.route_found && match_res.handler) {
                co_await (*match_res.handler)(ctx);
            } else if (match_res.method_not_allowed) {
                if (method_not_allowed_handler_) {
                    std::exception_ptr fallback_ex{nullptr};
                    try {
                        co_await method_not_allowed_handler_(ctx);
                    } catch (...) {
                        fallback_ex = std::current_exception();
                    }
                    if (fallback_ex) {
                        ctx.problem(StatusCode::MethodNotAllowed, "Method Not Allowed", "Method not allowed for requested route");
                    }
                } else {
                    ctx.problem(StatusCode::MethodNotAllowed, "Method Not Allowed", "Method " + std::string(to_string(req.method())) + " is not allowed for " + std::string(req.path()));
                }
            } else {
                if (not_found_handler_) {
                    std::exception_ptr fallback_ex{nullptr};
                    try {
                        co_await not_found_handler_(ctx);
                    } catch (...) {
                        fallback_ex = std::current_exception();
                    }
                    if (fallback_ex) {
                        ctx.problem(StatusCode::NotFound, "Not Found", "Requested route was not found");
                    }
                } else {
                    ctx.problem(StatusCode::NotFound, "Not Found", "Cannot " + std::string(to_string(req.method())) + " " + std::string(req.path()));
                }
            }
        };

        std::exception_ptr ex{nullptr};
        try {
            if (global_middleware_.empty()) {
                co_await execute_route();
            } else {
                Handler terminal = [&](Context&) -> core::Task<void> {
                    co_await execute_route();
                };
                co_await run_chain(global_middleware_, {}, 0, terminal, ctx);
            }
        } catch (...) {
            ex = std::current_exception();
        }

        if (ex) {
            if (error_handler_) {
                std::exception_ptr err_handler_ex{nullptr};
                try {
                    co_await error_handler_(ctx, ex);
                } catch (...) {
                    err_handler_ex = std::current_exception();
                }

                if (err_handler_ex) {
                    std::string detail = "Unknown error in custom error handler";
                    try {
                        std::rethrow_exception(err_handler_ex);
                    } catch (const std::exception& inner) {
                        detail = inner.what();
                    } catch (...) {}
                    ctx.problem(StatusCode::InternalServerError, "Internal Server Error", detail);
                }
            } else {
                std::string detail = "An internal server error occurred.";
                try {
                    std::rethrow_exception(ex);
                } catch (const std::exception& e) {
                    detail = e.what();
                } catch (...) {
                    detail = "Unknown exception occurred.";
                }
                ctx.problem(StatusCode::InternalServerError, "Internal Server Error", detail);
            }
        }
    }

private:
    std::unordered_map<std::string, StaticRouteEntry, StringHash, StringEq> static_routes_;
    std::unordered_map<std::string, WebSocketRouteEntry, StringHash, StringEq> ws_routes_;
    RadixTree tree_;
    std::vector<MiddlewareFn> global_middleware_;
    ErrorHandler error_handler_{nullptr};
    FallbackHandler not_found_handler_{nullptr};
    FallbackHandler method_not_allowed_handler_{nullptr};
};

// RouteGroup inline implementations
inline std::string join_paths(std::string_view a, std::string_view b) {
    if (a.empty() || a == "/") {
        if (b.empty()) return "/";
        if (b.starts_with('/')) return std::string(b);
        return "/" + std::string(b);
    }
    if (a.ends_with('/')) {
        if (b.starts_with('/')) return std::string(a) + std::string(b.substr(1));
        return std::string(a) + std::string(b);
    }
    if (b.starts_with('/')) return std::string(a) + std::string(b);
    return std::string(a) + "/" + std::string(b);
}

inline RouteGroup RouteGroup::group(std::string_view sub_prefix) {
    return RouteGroup(router_, join_paths(prefix_, sub_prefix), middleware_);
}

template <typename F>
RouteGroup& RouteGroup::get(std::string_view path, F&& handler) {
    router_.add_route(Method::GET, join_paths(prefix_, path), middleware_, {}, std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::get(std::string_view path, std::vector<MiddlewareFn> per_route, F&& handler) {
    router_.add_route(Method::GET, join_paths(prefix_, path), middleware_, std::move(per_route), std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::post(std::string_view path, F&& handler) {
    router_.add_route(Method::POST, join_paths(prefix_, path), middleware_, {}, std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::post(std::string_view path, std::vector<MiddlewareFn> per_route, F&& handler) {
    router_.add_route(Method::POST, join_paths(prefix_, path), middleware_, std::move(per_route), std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::put(std::string_view path, F&& handler) {
    router_.add_route(Method::PUT, join_paths(prefix_, path), middleware_, {}, std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::put(std::string_view path, std::vector<MiddlewareFn> per_route, F&& handler) {
    router_.add_route(Method::PUT, join_paths(prefix_, path), middleware_, std::move(per_route), std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::del(std::string_view path, F&& handler) {
    router_.add_route(Method::DELETE, join_paths(prefix_, path), middleware_, {}, std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::del(std::string_view path, std::vector<MiddlewareFn> per_route, F&& handler) {
    router_.add_route(Method::DELETE, join_paths(prefix_, path), middleware_, std::move(per_route), std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::patch(std::string_view path, F&& handler) {
    router_.add_route(Method::PATCH, join_paths(prefix_, path), middleware_, {}, std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::patch(std::string_view path, std::vector<MiddlewareFn> per_route, F&& handler) {
    router_.add_route(Method::PATCH, join_paths(prefix_, path), middleware_, std::move(per_route), std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::all(std::string_view path, F&& handler) {
    router_.all(join_paths(prefix_, path), middleware_, {}, std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::all(std::string_view path, std::vector<MiddlewareFn> per_route, F&& handler) {
    router_.all(join_paths(prefix_, path), middleware_, std::move(per_route), std::forward<F>(handler));
    return *this;
}

template <typename ClusterT, typename OptionsT>
RouteGroup& RouteGroup::proxy(std::string_view path,
                              ClusterT cluster,
                              OptionsT options,
                              std::vector<MiddlewareFn> per_route) {
    router_.all(join_paths(prefix_, path), middleware_, std::move(per_route),
        make_proxy_handler(std::move(cluster), std::move(options)));
    return *this;
}

struct CachedStaticFile {
    time_t mtime{0};
    long mtime_nsec{0};
    std::string content;
    std::string content_type;
    std::string content_encoding;
};

inline Router& Router::static_files(std::string_view prefix, std::string_view directory, StaticFilesOptions options) {
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
        while (!rel.empty() && rel.front() == '/') {
            rel.remove_prefix(1);
        }
        if (rel.empty()) {
            if (!options.index_file.empty()) {
                rel = options.index_file;
            } else {
                ctx.res().status(StatusCode::NotFound).text("Not Found");
                return;
            }
        }

        // Path traversal guard
        if (rel.find("..") != std::string_view::npos || 
            rel.find('\\') != std::string_view::npos ||
            rel.find('\0') != std::string_view::npos) {
            ctx.res().status(StatusCode::NotFound).text("Not Found");
            return;
        }

        std::string full_path = base_dir + "/" + std::string(rel);
        std::string_view content_type = Response::infer_mime_type(full_path);

        auto accept_enc = ctx.req().header("accept-encoding");
        bool accept_br = options.precompressed && accept_enc && accept_enc->find("br") != std::string_view::npos;
        bool accept_gz = options.precompressed && accept_enc && (accept_enc->find("gzip") != std::string_view::npos || accept_enc->find("deflate") != std::string_view::npos);

        auto try_serve = [&](const std::string& path, std::string_view encoding) -> bool {
            struct stat st{};
            if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                return false;
            }

#if defined(__linux__)
            long current_nsec = st.st_mtim.tv_nsec;
#elif defined(__APPLE__)
            long current_nsec = st.st_mtimespec.tv_nsec;
#else
            long current_nsec = 0;
#endif

            if (options.cache_in_memory) {
                std::shared_lock lock(*mtx);
                auto it = cache->find(path);
                if (it != cache->end()) {
                    const auto& entry = it->second;
                    if (entry.mtime == st.st_mtime && entry.mtime_nsec == current_nsec) {
                        ctx.res().header("Content-Type", entry.content_type);
                        if (!entry.content_encoding.empty()) {
                            ctx.res().header("Content-Encoding", entry.content_encoding);
                        }
                        ctx.res().body(entry.content);
                        return true;
                    }
                }
            }

            int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0) return false;

            std::string data;
            data.resize(static_cast<size_t>(st.st_size));
            size_t total_read = 0;
            while (total_read < data.size()) {
                ssize_t n = ::read(fd, data.data() + total_read, data.size() - total_read);
                if (n <= 0) break;
                total_read += static_cast<size_t>(n);
            }
            ::close(fd);

            if (total_read != data.size()) return false;

            if (options.cache_in_memory) {
                std::unique_lock lock(*mtx);
                (*cache)[path] = CachedStaticFile{
                    .mtime = st.st_mtime,
                    .mtime_nsec = current_nsec,
                    .content = data,
                    .content_type = std::string(content_type),
                    .content_encoding = std::string(encoding)
                };
            }

            ctx.res().header("Content-Type", content_type);
            if (!encoding.empty()) {
                ctx.res().header("Content-Encoding", encoding);
            }
            ctx.res().body(std::move(data));
            return true;
        };

        if (accept_br && try_serve(full_path + ".br", "br")) return;
        if (accept_gz && try_serve(full_path + ".gz", "gzip")) return;
        if (try_serve(full_path, "")) return;

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

inline RouteGroup& RouteGroup::static_files(std::string_view path, std::string_view directory, StaticFilesOptions options) {
    router_.static_files(join_paths(prefix_, path), directory, std::move(options));
    return *this;
}

} // namespace aegon::http
