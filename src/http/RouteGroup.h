#pragma once

#include "http/RouteRegistrar.h"
#include <string>
#include <string_view>
#include <vector>

namespace aegon::http {

struct StaticFilesOptions {
    bool precompressed{true};
    bool cache_in_memory{true};
    std::string index_file{"index.html"};
};

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

class Router;

/**
 * @brief Scoped route grouping with path prefixing and middleware inheritance.
 */
class RouteGroup : public RouteRegistrar<RouteGroup> {
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

    /**
     * @brief Registers a route under this group with inherited group middleware and optional per-route middleware.
     */
    template <typename F>
    RouteGroup& add_route(Method method, std::string_view pattern, std::vector<MiddlewareFn> route_mw, F&& handler);

    template <typename F>
    RouteGroup& add_route(Method method, std::string_view pattern, F&& handler) {
        return add_route(method, pattern, {}, std::forward<F>(handler));
    }

    /**
     * @brief Serves static files rooted at `directory` under this group's prefix.
     */
    RouteGroup& static_files(std::string_view path, std::string_view directory, StaticFilesOptions options = {});

    [[nodiscard]] std::string_view prefix() const noexcept { return prefix_; }
    [[nodiscard]] const std::vector<MiddlewareFn>& middleware() const noexcept { return middleware_; }

private:
    Router& router_;
    std::string prefix_;
    std::vector<MiddlewareFn> middleware_;
};

} // namespace aegon::http
