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

namespace aegon::http {

class Router {
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
        tree_.insert(method, pattern, make_handler(std::forward<F>(handler)));
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
        return tree_.match(req);
    }

private:
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
