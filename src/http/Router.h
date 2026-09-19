#pragma once

#include "http/Protocol.h"
#include "http/Context.h"
#include "http/RadixTree.h"
#include "http/RouteGroup.h"
#include "http/Middleware.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <type_traits>
#include <concepts>
#include <functional>
#include <unordered_map>
#include <vector>

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

} // namespace aegon::http
