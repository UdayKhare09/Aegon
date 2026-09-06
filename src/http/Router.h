#pragma once

#include "http/Protocol.h"
#include "http/Context.h"
#include "core/Task.h"
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>

namespace aegon::http {

using Handler = std::function<core::Task<void>(Context&)>;

struct Route {
    Method method;
    std::string pattern;
    std::vector<std::string> param_names;
    std::vector<std::string> static_segments;
    bool is_wildcard{false};
    Handler handler;
};

class Router {
public:
    Router() = default;

    void add_route(Method method, std::string_view pattern, Handler handler);

    void get(std::string_view pattern, Handler handler) {
        add_route(Method::GET, pattern, std::move(handler));
    }

    void post(std::string_view pattern, Handler handler) {
        add_route(Method::POST, pattern, std::move(handler));
    }

    void put(std::string_view pattern, Handler handler) {
        add_route(Method::PUT, pattern, std::move(handler));
    }

    void del(std::string_view pattern, Handler handler) {
        add_route(Method::DELETE, pattern, std::move(handler));
    }

    void patch(std::string_view pattern, Handler handler) {
        add_route(Method::PATCH, pattern, std::move(handler));
    }

    struct MatchResult {
        const Handler* handler{nullptr};
        bool route_found{false};
        bool method_not_allowed{false};
    };

    /**
     * @brief Matches incoming request against registered routes and populates route params.
     */
    [[nodiscard]] MatchResult match(Request& req) const;

private:
    std::vector<Route> routes_;
};

} // namespace aegon::http
