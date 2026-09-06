#pragma once

#include "http/Protocol.h"
#include "http/Context.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <type_traits>
#include <concepts>
#include <functional>

namespace aegon::http {

class Router;

class RouteGroup {
public:
    RouteGroup(Router& router, std::string prefix)
        : router_(router), prefix_(std::move(prefix)) {}

    /**
     * @brief Creates a nested sub-group with an accumulated path prefix.
     */
    [[nodiscard]] RouteGroup group(std::string_view sub_prefix);

    template <typename F>
    RouteGroup& get(std::string_view path, F&& handler);

    template <typename F>
    RouteGroup& post(std::string_view path, F&& handler);

    template <typename F>
    RouteGroup& put(std::string_view path, F&& handler);

    template <typename F>
    RouteGroup& del(std::string_view path, F&& handler);

    template <typename F>
    RouteGroup& patch(std::string_view path, F&& handler);

    [[nodiscard]] std::string_view prefix() const noexcept { return prefix_; }

private:
    Router& router_;
    std::string prefix_;
};

} // namespace aegon::http
