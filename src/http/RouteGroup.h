#pragma once

#include "http/Protocol.h"
#include "http/Context.h"
#include "http/Middleware.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <type_traits>
#include <concepts>
#include <functional>
#include <vector>

namespace aegon::http {

class Router;

class RouteGroup {
public:
    RouteGroup(Router& router, std::string prefix, std::vector<MiddlewareFn> inherited = {})
        : router_(router), prefix_(std::move(prefix)), middleware_(std::move(inherited)) {}

    /**
     * @brief Appends middleware to this route group's execution chain.
     */
    template <typename F>
    RouteGroup& use(F&& mw) {
        middleware_.push_back(make_middleware(std::forward<F>(mw)));
        return *this;
    }

    /**
     * @brief Creates a nested sub-group inheriting this group's middleware chain and accumulating path prefix.
     */
    [[nodiscard]] RouteGroup group(std::string_view sub_prefix);

    template <typename F>
    RouteGroup& get(std::string_view path, F&& handler);

    template <typename F>
    RouteGroup& get(std::string_view path, std::vector<MiddlewareFn> per_route, F&& handler);

    template <typename F>
    RouteGroup& post(std::string_view path, F&& handler);

    template <typename F>
    RouteGroup& post(std::string_view path, std::vector<MiddlewareFn> per_route, F&& handler);

    template <typename F>
    RouteGroup& put(std::string_view path, F&& handler);

    template <typename F>
    RouteGroup& put(std::string_view path, std::vector<MiddlewareFn> per_route, F&& handler);

    template <typename F>
    RouteGroup& del(std::string_view path, F&& handler);

    template <typename F>
    RouteGroup& del(std::string_view path, std::vector<MiddlewareFn> per_route, F&& handler);

    template <typename F>
    RouteGroup& patch(std::string_view path, F&& handler);

    template <typename F>
    RouteGroup& patch(std::string_view path, std::vector<MiddlewareFn> per_route, F&& handler);

    template <typename F>
    RouteGroup& all(std::string_view path, F&& handler);

    template <typename F>
    RouteGroup& all(std::string_view path, std::vector<MiddlewareFn> per_route, F&& handler);

    template <typename ClusterT, typename OptionsT>
    RouteGroup& proxy(std::string_view path,
                      ClusterT cluster,
                      OptionsT options,
                      std::vector<MiddlewareFn> per_route = {});

    [[nodiscard]] std::string_view prefix() const noexcept { return prefix_; }
    [[nodiscard]] const std::vector<MiddlewareFn>& middleware() const noexcept { return middleware_; }

private:
    Router& router_;
    std::string prefix_;
    std::vector<MiddlewareFn> middleware_;
};

} // namespace aegon::http
