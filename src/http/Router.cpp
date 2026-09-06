#include "http/Router.h"
#include <sstream>

namespace aegon::http {

namespace {

std::vector<std::string_view> split_path(std::string_view path) {
    std::vector<std::string_view> segments;
    while (!path.empty()) {
        while (!path.empty() && path.front() == '/') {
            path.remove_prefix(1);
        }
        if (path.empty()) break;
        size_t next_slash = path.find('/');
        if (next_slash == std::string_view::npos) {
            segments.push_back(path);
            break;
        }
        segments.push_back(path.substr(0, next_slash));
        path.remove_prefix(next_slash + 1);
    }
    return segments;
}

} // anonymous namespace

void Router::add_route(Method method, std::string_view pattern, Handler handler) {
    Route route;
    route.method = method;
    route.pattern = std::string(pattern);
    route.handler = std::move(handler);

    auto segs = split_path(pattern);
    for (auto s : segs) {
        if (s.starts_with(':')) {
            route.param_names.emplace_back(s.substr(1));
            route.static_segments.emplace_back(""); // empty indicates param placeholder
        } else if (s == "*") {
            route.is_wildcard = true;
            route.static_segments.emplace_back("*");
            break;
        } else {
            route.param_names.emplace_back("");
            route.static_segments.emplace_back(s);
        }
    }

    routes_.push_back(std::move(route));
}

Router::MatchResult Router::match(Request& req) const {
    std::string_view path = req.path();
    auto req_segs = split_path(path);

    bool path_matched_any_method = false;

    for (const auto& route : routes_) {
        // Check segment counts
        if (!route.is_wildcard && req_segs.size() != route.static_segments.size()) {
            continue;
        }
        if (route.is_wildcard && req_segs.size() < (route.static_segments.size() - 1)) {
            continue;
        }

        bool match = true;
        std::vector<std::pair<std::string_view, std::string_view>> extracted_params;

        for (size_t i = 0; i < route.static_segments.size(); ++i) {
            if (route.static_segments[i] == "*") {
                // Wildcard matches rest of path
                break;
            }

            if (!route.param_names[i].empty()) {
                // Named parameter
                extracted_params.emplace_back(route.param_names[i], req_segs[i]);
            } else {
                // Static segment check
                if (req_segs[i] != route.static_segments[i]) {
                    match = false;
                    break;
                }
            }
        }

        if (match) {
            path_matched_any_method = true;
            if (req.method() == route.method) {
                // Populate params into request
                for (const auto& [k, v] : extracted_params) {
                    req.add_param(k, v);
                }
                return MatchResult{
                    .handler = &route.handler,
                    .route_found = true,
                    .method_not_allowed = false
                };
            }
        }
    }

    return MatchResult{
        .handler = nullptr,
        .route_found = false,
        .method_not_allowed = path_matched_any_method
    };
}

} // namespace aegon::http
