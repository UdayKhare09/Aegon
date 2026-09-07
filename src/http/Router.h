#pragma once

#include "http/Protocol.h"
#include "http/Context.h"
#include "http/RadixTree.h"
#include "http/RouteGroup.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <type_traits>
#include <concepts>
#include <functional>
#include <unordered_map>

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

    template <typename F>
    Router& add_route(Method method, std::string_view pattern, F&& handler) {
        auto h = make_handler(std::forward<F>(handler));

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
    Router& get(std::string_view pattern, F&& handler) {
        return add_route(Method::GET, pattern, std::forward<F>(handler));
    }

    template <typename F>
    Router& post(std::string_view pattern, F&& handler) {
        return add_route(Method::POST, pattern, std::forward<F>(handler));
    }

    template <typename F>
    Router& put(std::string_view pattern, F&& handler) {
        return add_route(Method::PUT, pattern, std::forward<F>(handler));
    }

    template <typename F>
    Router& del(std::string_view pattern, F&& handler) {
        return add_route(Method::DELETE, pattern, std::forward<F>(handler));
    }

    template <typename F>
    Router& patch(std::string_view pattern, F&& handler) {
        return add_route(Method::PATCH, pattern, std::forward<F>(handler));
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

private:
    std::unordered_map<std::string, StaticRouteEntry, StringHash, StringEq> static_routes_;
    RadixTree tree_;
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
    return RouteGroup(router_, join_paths(prefix_, sub_prefix));
}

template <typename F>
RouteGroup& RouteGroup::get(std::string_view path, F&& handler) {
    router_.get(join_paths(prefix_, path), std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::post(std::string_view path, F&& handler) {
    router_.post(join_paths(prefix_, path), std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::put(std::string_view path, F&& handler) {
    router_.put(join_paths(prefix_, path), std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::del(std::string_view path, F&& handler) {
    router_.del(join_paths(prefix_, path), std::forward<F>(handler));
    return *this;
}

template <typename F>
RouteGroup& RouteGroup::patch(std::string_view path, F&& handler) {
    router_.patch(join_paths(prefix_, path), std::forward<F>(handler));
    return *this;
}

} // namespace aegon::http
