#pragma once

#include "http/Protocol.h"
#include "http/Context.h"
#include "http/Middleware.h"
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <functional>
#include <optional>

namespace aegon::http::middleware {

/**
 * @brief Matches an origin string against a pattern supporting wildcards.
 *
 * Supported patterns:
 * - https://wildcard.example.com (e.g. *.example.com)
 * - http://localhost:*
 * - https://dev-*.tenant.io
 */
inline bool match_origin_pattern(std::string_view pattern, std::string_view origin) noexcept {
    size_t star_pos = pattern.find('*');
    if (star_pos == std::string_view::npos) {
        return pattern == origin;
    }

    std::string_view prefix = pattern.substr(0, star_pos);
    if (!origin.starts_with(prefix)) {
        return false;
    }

    std::string_view rem_pattern = pattern.substr(star_pos + 1);
    std::string_view rem_origin = origin.substr(prefix.size());

    size_t second_star = rem_pattern.find('*');
    if (second_star == std::string_view::npos) {
        return rem_origin.ends_with(rem_pattern) && rem_origin.size() >= rem_pattern.size();
    }

    std::string_view mid = rem_pattern.substr(0, second_star);
    std::string_view suffix = rem_pattern.substr(second_star + 1);

    size_t mid_pos = rem_origin.find(mid);
    if (mid_pos == std::string_view::npos) {
        return false;
    }

    std::string_view end_origin = rem_origin.substr(mid_pos + mid.size());
    return end_origin.ends_with(suffix) && end_origin.size() >= suffix.size();
}

/**
 * @brief Enterprise-grade CORS configuration aggregate struct.
 */
struct CorsConfig {
    // 1. Exact origin matching (e.g. {"https://myapp.com", "http://localhost:3000"} or {"*"})
    std::vector<std::string> allowed_origins{};

    // 2. Spring Boot-style subdomain glob/wildcard patterns (e.g. {"https://*.myapp.com", "http://localhost:*"})
    std::vector<std::string> origin_patterns{};

    // 3. Spring Boot-style dynamic origin validator lambda (e.g. for runtime multi-tenant DB lookups)
    std::function<bool(std::string_view origin)> origin_validator{nullptr};

    // 4. Allowed HTTP methods for preflight
    std::vector<Method> allowed_methods{
        Method::GET, Method::POST, Method::PUT, Method::DELETE,
        Method::PATCH, Method::OPTIONS, Method::HEAD
    };

    // 5. Allowed request headers (supports {"*"} or explicit list)
    std::vector<std::string> allowed_headers{"*"};

    // 6. Headers exposed to client JavaScript
    std::vector<std::string> expose_headers{};

    // 7. Allow cookies / authorization credentials
    bool allow_credentials{false};

    // 8. W3C Private Network Access (PNA / RFC1918) for public-to-private requests
    bool allow_private_network{false};

    // 9. Preflight cache duration (default: 24 hours)
    std::chrono::seconds max_age{86400};

    // 10. Automatically emit Vary headers for proxy/CDN isolation
    bool vary_header{true};

    // Preset: Permissive (public APIs, wildcard origin)
    static CorsConfig permissive() {
        CorsConfig c;
        c.allowed_origins = {"*"};
        return c;
    }

    // Preset: Strict (authenticated enterprise APIs with credentials)
    static CorsConfig strict(std::vector<std::string> origins, bool credentials = true) {
        CorsConfig c;
        c.allowed_origins = std::move(origins);
        c.allow_credentials = credentials;
        c.allowed_headers = {"Authorization", "Content-Type", "X-Requested-With", "Accept", "Origin", "X-Request-ID"};
        c.expose_headers = {"X-Request-ID", "Content-Length"};
        return c;
    }
};

namespace detail {

inline bool is_origin_allowed(const CorsConfig& config, std::string_view origin, bool& is_wildcard) noexcept {
    is_wildcard = false;

    // 1. Check exact allowed_origins
    for (const auto& allowed : config.allowed_origins) {
        if (allowed == "*") {
            is_wildcard = true;
            return true;
        }
        if (allowed == origin) {
            return true;
        }
    }

    // 2. Check origin_patterns
    for (const auto& pattern : config.origin_patterns) {
        if (match_origin_pattern(pattern, origin)) {
            return true;
        }
    }

    // 3. Check dynamic origin_validator lambda
    if (config.origin_validator && config.origin_validator(origin)) {
        return true;
    }

    return false;
}

inline std::string join_strings(const std::vector<std::string>& list, std::string_view delimiter = ", ") {
    std::string out;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i > 0) out += delimiter;
        out += list[i];
    }
    return out;
}

inline std::string join_methods(const std::vector<Method>& methods) {
    std::string out;
    for (size_t i = 0; i < methods.size(); ++i) {
        if (i > 0) out += ", ";
        out += to_string(methods[i]);
    }
    return out;
}

} // namespace detail

/**
 * @brief Generates an enterprise-grade CORS middleware.
 *
 * Automatically handles:
 * - Preflight OPTIONS request validation, caching, and early 204 No Content short-circuit.
 * - Dynamic origin reflection with Vary: Origin when credentials are enabled.
 * - Subdomain wildcard pattern matching and dynamic tenant origin validation.
 * - W3C Private Network Access (PNA).
 */
inline MiddlewareFn cors(CorsConfig config = CorsConfig::permissive()) {
    // Pre-serialize static headers once at middleware initialization to achieve zero per-request allocation
    std::string methods_str = detail::join_methods(config.allowed_methods);
    std::string allowed_headers_str = detail::join_strings(config.allowed_headers);
    std::string expose_headers_str = detail::join_strings(config.expose_headers);
    std::string max_age_str = std::to_string(config.max_age.count());
    bool is_wildcard_headers = (config.allowed_headers.size() == 1 && config.allowed_headers[0] == "*");

    return [config = std::move(config),
            methods_str = std::move(methods_str),
            allowed_headers_str = std::move(allowed_headers_str),
            expose_headers_str = std::move(expose_headers_str),
            max_age_str = std::move(max_age_str),
            is_wildcard_headers](Context& ctx, Next next) -> core::Task<void> {

        auto origin_opt = ctx.req().headers().get("origin");
        if (!origin_opt) {
            // Not a cross-origin request
            co_await next(ctx);
            co_return;
        }

        std::string_view origin = *origin_opt;
        bool is_wildcard_origin = false;
        bool allowed = detail::is_origin_allowed(config, origin, is_wildcard_origin);

        // Preflight OPTIONS detection
        bool is_preflight = (ctx.req().method() == Method::OPTIONS) &&
                            ctx.req().headers().contains("access-control-request-method");

        if (is_preflight) {
            if (!allowed) {
                // Disallowed origin during preflight -> short-circuit without CORS allow headers
                ctx.res().status(StatusCode::NoContent);
                co_return;
            }

            // Set Allow-Origin
            if (config.allow_credentials) {
                ctx.res().header("access-control-allow-origin", origin);
                ctx.res().header("access-control-allow-credentials", "true");
            } else if (is_wildcard_origin) {
                ctx.res().header("access-control-allow-origin", "*");
            } else {
                ctx.res().header("access-control-allow-origin", origin);
            }

            // Set Allow-Methods
            ctx.res().header("access-control-allow-methods", methods_str);

            // Set Allow-Headers
            if (is_wildcard_headers) {
                auto req_hdrs = ctx.req().headers().get("access-control-request-headers");
                if (req_hdrs) {
                    ctx.res().header("access-control-allow-headers", *req_hdrs);
                } else {
                    ctx.res().header("access-control-allow-headers", "*");
                }
            } else {
                ctx.res().header("access-control-allow-headers", allowed_headers_str);
            }

            // Set Max-Age
            ctx.res().header("access-control-max-age", max_age_str);

            // Set Private Network Access if requested and enabled
            if (config.allow_private_network) {
                auto pna_req = ctx.req().headers().get("access-control-request-private-network");
                if (pna_req && *pna_req == "true") {
                    ctx.res().header("access-control-allow-private-network", "true");
                }
            }

            if (config.vary_header) {
                ctx.res().header("vary", "Origin, Access-Control-Request-Method, Access-Control-Request-Headers");
            }

            ctx.res().status(StatusCode::NoContent);
            co_return; // SHORT-CIRCUIT: preflight does not invoke route handler
        }

        // Standard Cross-Origin Request
        if (allowed) {
            if (config.allow_credentials) {
                ctx.res().header("access-control-allow-origin", origin);
                ctx.res().header("access-control-allow-credentials", "true");
            } else if (is_wildcard_origin) {
                ctx.res().header("access-control-allow-origin", "*");
            } else {
                ctx.res().header("access-control-allow-origin", origin);
            }

            if (!expose_headers_str.empty()) {
                ctx.res().header("access-control-expose-headers", expose_headers_str);
            }

            if (config.vary_header) {
                ctx.res().header("vary", "Origin");
            }
        }

        co_await next(ctx);
    };
}

} // namespace aegon::http::middleware
