#pragma once

#include <string_view>
#include <string>
#include <optional>
#include <chrono>

namespace aegon::http {

/**
 * @brief Controls the SameSite attribute of HTTP cookies per RFC 6265bis.
 */
enum class SameSite {
    Default, ///< Directive omitted from Set-Cookie
    Lax,     ///< Sent on top-level safe navigations (default in modern browsers)
    Strict,  ///< Never sent on cross-site requests
    None     ///< Sent on all cross-site requests (requires Secure attribute)
};

/**
 * @brief Configuration options for setting HTTP cookies via ctx.res().set_cookie({ ... })
 * using modern C++ designated initializers.
 */
struct CookieOptions {
    std::string_view name;
    std::string_view value;
    std::string_view path{"/"};
    std::string_view domain{};
    std::optional<std::chrono::seconds> max_age{std::nullopt};
    SameSite same_site{SameSite::Lax};
    bool http_only{false};
    bool secure{false};
    bool partitioned{false}; ///< CHIPS (Cookies Having Independent Partitioned State)
};

/**
 * @brief Formats CookieOptions into a valid RFC 6265 Set-Cookie header value.
 */
inline std::string format_cookie(const CookieOptions& opts) {
    std::string out;
    out.reserve(opts.name.size() + opts.value.size() + opts.path.size() + opts.domain.size() + 64);
    out.append(opts.name);
    out.push_back('=');
    out.append(opts.value);

    if (!opts.path.empty()) {
        out.append("; Path=");
        out.append(opts.path);
    }

    if (!opts.domain.empty()) {
        out.append("; Domain=");
        out.append(opts.domain);
    }

    if (opts.max_age) {
        out.append("; Max-Age=");
        out.append(std::to_string(opts.max_age->count()));
    }

    switch (opts.same_site) {
        case SameSite::Strict:
            out.append("; SameSite=Strict");
            break;
        case SameSite::Lax:
            out.append("; SameSite=Lax");
            break;
        case SameSite::None:
            out.append("; SameSite=None");
            break;
        case SameSite::Default:
            break;
    }

    if (opts.secure) {
        out.append("; Secure");
    }

    if (opts.http_only) {
        out.append("; HttpOnly");
    }

    if (opts.partitioned) {
        out.append("; Partitioned");
    }

    return out;
}

} // namespace aegon::http
