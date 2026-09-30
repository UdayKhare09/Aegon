#pragma once

#include "http/Protocol.h"
#include "http/Context.h"
#include "http/Middleware.h"
#include "core/Task.h"
#include <string_view>
#include <vector>
#include <utility>

namespace aegon::http {

namespace detail {

template <typename F>
Handler make_handler(F&& f) {
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

} // namespace detail

/**
 * @brief CRTP base providing uniform HTTP verb route registration methods for Router and RouteGroup.
 */
template <typename Derived>
class RouteRegistrar {
    Derived& derived() noexcept { return static_cast<Derived&>(*this); }

public:
    template <typename F>
    Derived& get(std::string_view pattern, F&& handler) {
        return derived().add_route(Method::GET, pattern, {}, std::forward<F>(handler));
    }

    template <typename F>
    Derived& get(std::string_view pattern, std::vector<MiddlewareFn> route_mw, F&& handler) {
        return derived().add_route(Method::GET, pattern, std::move(route_mw), std::forward<F>(handler));
    }

    template <typename F>
    Derived& post(std::string_view pattern, F&& handler) {
        return derived().add_route(Method::POST, pattern, {}, std::forward<F>(handler));
    }

    template <typename F>
    Derived& post(std::string_view pattern, std::vector<MiddlewareFn> route_mw, F&& handler) {
        return derived().add_route(Method::POST, pattern, std::move(route_mw), std::forward<F>(handler));
    }

    template <typename F>
    Derived& put(std::string_view pattern, F&& handler) {
        return derived().add_route(Method::PUT, pattern, {}, std::forward<F>(handler));
    }

    template <typename F>
    Derived& put(std::string_view pattern, std::vector<MiddlewareFn> route_mw, F&& handler) {
        return derived().add_route(Method::PUT, pattern, std::move(route_mw), std::forward<F>(handler));
    }

    template <typename F>
    Derived& del(std::string_view pattern, F&& handler) {
        return derived().add_route(Method::DELETE, pattern, {}, std::forward<F>(handler));
    }

    template <typename F>
    Derived& del(std::string_view pattern, std::vector<MiddlewareFn> route_mw, F&& handler) {
        return derived().add_route(Method::DELETE, pattern, std::move(route_mw), std::forward<F>(handler));
    }

    template <typename F>
    Derived& patch(std::string_view pattern, F&& handler) {
        return derived().add_route(Method::PATCH, pattern, {}, std::forward<F>(handler));
    }

    template <typename F>
    Derived& patch(std::string_view pattern, std::vector<MiddlewareFn> route_mw, F&& handler) {
        return derived().add_route(Method::PATCH, pattern, std::move(route_mw), std::forward<F>(handler));
    }

    template <typename F>
    Derived& all(std::string_view pattern, F&& handler) {
        return all(pattern, {}, std::forward<F>(handler));
    }

    template <typename F>
    Derived& all(std::string_view pattern, std::vector<MiddlewareFn> route_mw, F&& handler) {
        static constexpr Method all_methods[] = {
            Method::GET, Method::POST, Method::PUT, Method::DELETE,
            Method::PATCH, Method::HEAD, Method::OPTIONS
        };
        Handler h = detail::make_handler(std::forward<F>(handler));
        for (Method m : all_methods) {
            derived().add_route(m, pattern, route_mw, h);
        }
        return derived();
    }

    template <typename ClusterT, typename OptionsT>
    Derived& proxy(std::string_view pattern,
                   ClusterT cluster,
                   OptionsT options,
                   std::vector<MiddlewareFn> middlewares = {}) {
        return derived().all(pattern, std::move(middlewares),
                             make_proxy_handler(std::move(cluster), std::move(options)));
    }
};

} // namespace aegon::http
