#pragma once

#include "http/Context.h"
#include "http/Middleware.h"
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <utility>

namespace aegon::http::middleware {

enum class FrameOption : uint8_t {
    Deny,         // X-Frame-Options: DENY (Recommended clickjacking defense)
    SameOrigin,   // X-Frame-Options: SAMEORIGIN
    Disabled      // Omit header (e.g. when using CSP frame-ancestors instead)
};

constexpr std::string_view to_string(FrameOption opt) noexcept {
    switch (opt) {
        case FrameOption::Deny:       return "DENY";
        case FrameOption::SameOrigin: return "SAMEORIGIN";
        case FrameOption::Disabled:   return "";
    }
    return "";
}

enum class ReferrerPolicy : uint8_t {
    NoReferrer,
    NoReferrerWhenDowngrade,
    SameOrigin,
    Origin,
    StrictOrigin,
    OriginWhenCrossOrigin,
    StrictOriginWhenCrossOrigin, // Recommended OWASP default
    UnsafeUrl
};

constexpr std::string_view to_string(ReferrerPolicy pol) noexcept {
    switch (pol) {
        case ReferrerPolicy::NoReferrer:                  return "no-referrer";
        case ReferrerPolicy::NoReferrerWhenDowngrade:     return "no-referrer-when-downgrade";
        case ReferrerPolicy::SameOrigin:                  return "same-origin";
        case ReferrerPolicy::Origin:                      return "origin";
        case ReferrerPolicy::StrictOrigin:                return "strict-origin";
        case ReferrerPolicy::OriginWhenCrossOrigin:        return "origin-when-cross-origin";
        case ReferrerPolicy::StrictOriginWhenCrossOrigin:  return "strict-origin-when-cross-origin";
        case ReferrerPolicy::UnsafeUrl:                   return "unsafe-url";
    }
    return "strict-origin-when-cross-origin";
}

enum class CrossOriginOpenerPolicy : uint8_t {
    SameOrigin,             // Isolates window context against Spectre attacks
    SameOriginAllowPopups,
    UnsafeNone,
    Disabled
};

constexpr std::string_view to_string(CrossOriginOpenerPolicy coop) noexcept {
    switch (coop) {
        case CrossOriginOpenerPolicy::SameOrigin:            return "same-origin";
        case CrossOriginOpenerPolicy::SameOriginAllowPopups: return "same-origin-allow-popups";
        case CrossOriginOpenerPolicy::UnsafeNone:             return "unsafe-none";
        case CrossOriginOpenerPolicy::Disabled:               return "";
    }
    return "";
}

enum class CrossOriginEmbedderPolicy : uint8_t {
    RequireCorp,    // require-corp (Required for SharedArrayBuffer)
    Credentialless,
    UnsafeNone,
    Disabled
};

constexpr std::string_view to_string(CrossOriginEmbedderPolicy coep) noexcept {
    switch (coep) {
        case CrossOriginEmbedderPolicy::RequireCorp:    return "require-corp";
        case CrossOriginEmbedderPolicy::Credentialless: return "credentialless";
        case CrossOriginEmbedderPolicy::UnsafeNone:     return "unsafe-none";
        case CrossOriginEmbedderPolicy::Disabled:       return "";
    }
    return "";
}

enum class CrossOriginResourcePolicy : uint8_t {
    SameOrigin,     // Blocks unauthorized cross-origin reads
    SameSite,
    CrossOrigin,
    Disabled
};

constexpr std::string_view to_string(CrossOriginResourcePolicy corp) noexcept {
    switch (corp) {
        case CrossOriginResourcePolicy::SameOrigin:  return "same-origin";
        case CrossOriginResourcePolicy::SameSite:    return "same-site";
        case CrossOriginResourcePolicy::CrossOrigin: return "cross-origin";
        case CrossOriginResourcePolicy::Disabled:   return "";
    }
    return "";
}

struct HstsConfig {
    bool enabled{true};
    std::chrono::seconds max_age{31536000}; // 1 year (recommended)
    bool include_subdomains{true};
    bool preload{false};                     // Request inclusion in browser HSTS preload list

    [[nodiscard]] std::string serialize() const {
        if (!enabled) return "";
        std::string s = "max-age=" + std::to_string(max_age.count());
        if (include_subdomains) {
            s += "; includeSubDomains";
        }
        if (preload) {
            s += "; preload";
        }
        return s;
    }
};

/**
 * @brief Enterprise-grade security headers configuration aggregate struct.
 */
struct SecurityHeadersConfig {
    // 1. MIME Sniffing protection (X-Content-Type-Options: nosniff)
    bool content_type_nosniff{true};

    // 2. Clickjacking defense (X-Frame-Options: DENY / SAMEORIGIN)
    FrameOption frame_options{FrameOption::Deny};

    // 3. Modern XSS filter defense (X-XSS-Protection: 0 disables legacy buggy auditor per OWASP)
    bool xss_protection_disabled{true};

    // 4. Referrer information leakage control
    ReferrerPolicy referrer_policy{ReferrerPolicy::StrictOriginWhenCrossOrigin};

    // 5. HTTP Strict Transport Security (HSTS)
    HstsConfig hsts{};

    // 6. Content Security Policy (CSP)
    std::string content_security_policy{};
    bool csp_report_only{false}; // Emits Content-Security-Policy-Report-Only

    // 7. Permissions-Policy (Feature-Policy)
    std::string permissions_policy{};

    // 8. Cross-Origin Isolation
    CrossOriginOpenerPolicy coop{CrossOriginOpenerPolicy::SameOrigin};
    CrossOriginEmbedderPolicy coep{CrossOriginEmbedderPolicy::Disabled};
    CrossOriginResourcePolicy corp{CrossOriginResourcePolicy::SameOrigin};

    // 9. Sensitive Route Zero-Cache Protection
    // Emits Cache-Control: no-cache, no-store, max-age=0, must-revalidate and Pragma: no-cache
    bool prevent_browser_caching{false};

    // Preset: Full OWASP baseline for web applications and browser clients
    static SecurityHeadersConfig defaults() {
        return SecurityHeadersConfig{};
    }

    // Preset: Optimized for headless REST/JSON APIs
    static SecurityHeadersConfig api() {
        SecurityHeadersConfig c;
        c.frame_options = FrameOption::Deny;
        c.referrer_policy = ReferrerPolicy::NoReferrer;
        c.coop = CrossOriginOpenerPolicy::SameOrigin;
        c.corp = CrossOriginResourcePolicy::SameOrigin;
        return c;
    }
};

/**
 * @brief Generates an enterprise-grade SecurityHeaders middleware.
 *
 * Pre-serializes all security headers at initialization for zero runtime heap allocation.
 */
inline MiddlewareFn security_headers(SecurityHeadersConfig config = SecurityHeadersConfig::defaults()) {
    std::vector<std::pair<std::string, std::string>> static_headers;
    static_headers.reserve(12);

    if (config.content_type_nosniff) {
        static_headers.emplace_back("x-content-type-options", "nosniff");
    }

    if (config.frame_options != FrameOption::Disabled) {
        static_headers.emplace_back("x-frame-options", std::string(to_string(config.frame_options)));
    }

    if (config.xss_protection_disabled) {
        static_headers.emplace_back("x-xss-protection", "0");
    }

    static_headers.emplace_back("referrer-policy", std::string(to_string(config.referrer_policy)));

    if (config.hsts.enabled) {
        std::string hsts_val = config.hsts.serialize();
        if (!hsts_val.empty()) {
            static_headers.emplace_back("strict-transport-security", std::move(hsts_val));
        }
    }

    if (!config.content_security_policy.empty()) {
        std::string csp_name = config.csp_report_only
            ? "content-security-policy-report-only"
            : "content-security-policy";
        static_headers.emplace_back(std::move(csp_name), config.content_security_policy);
    }

    if (!config.permissions_policy.empty()) {
        static_headers.emplace_back("permissions-policy", config.permissions_policy);
    }

    if (config.coop != CrossOriginOpenerPolicy::Disabled) {
        static_headers.emplace_back("cross-origin-opener-policy", std::string(to_string(config.coop)));
    }

    if (config.coep != CrossOriginEmbedderPolicy::Disabled) {
        static_headers.emplace_back("cross-origin-embedder-policy", std::string(to_string(config.coep)));
    }

    if (config.corp != CrossOriginResourcePolicy::Disabled) {
        static_headers.emplace_back("cross-origin-resource-policy", std::string(to_string(config.corp)));
    }

    if (config.prevent_browser_caching) {
        static_headers.emplace_back("cache-control", "no-cache, no-store, max-age=0, must-revalidate");
        static_headers.emplace_back("pragma", "no-cache");
        static_headers.emplace_back("expires", "0");
    }

    return [static_headers = std::move(static_headers)](Context& ctx, Next next) -> core::Task<void> {
        for (const auto& [name, value] : static_headers) {
            ctx.res().header(name, value);
        }
        co_await next(ctx);
    };
}

} // namespace aegon::http::middleware
