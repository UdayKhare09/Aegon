#pragma once

#include "http/Context.h"
#include "http/Middleware.h"
#include "http/Protocol.h"
#include "http/ProblemDetails.h"
#include "http/jwt/JwtAlgorithm.h"
#include "http/jwt/JwtVerifier.h"
#include "core/Task.h"

#include <string>
#include <string_view>
#include <functional>
#include <optional>
#include <memory>
#include <utility>

namespace aegon::http::middleware {

namespace detail {

inline void default_send_unauthorized(
    Context& ctx,
    std::string_view detail,
    std::optional<std::string_view> www_authenticate = std::nullopt
) {
    ctx.res().status(StatusCode::Unauthorized);
    ctx.res().header("Content-Type", "application/problem+json");
    if (www_authenticate.has_value()) {
        ctx.res().header("WWW-Authenticate", *www_authenticate);
    }
    ProblemDetails problem{
        .type = "about:blank",
        .title = "Unauthorized",
        .status = 401,
        .detail = std::string(detail)
    };
    ctx.res().body(problem.to_json());
}

inline std::string_view trim_whitespace(std::string_view sv) noexcept {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return sv;
}

inline bool iequals_prefix(std::string_view s, std::string_view prefix) noexcept {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        char c1 = s[i];
        char c2 = prefix[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return false;
    }
    return true;
}

} // namespace detail

// -----------------------------------------------------------------------------
// Options Structs
// -----------------------------------------------------------------------------

template <typename T>
struct BearerAuthOptions {
    std::function<core::Task<std::optional<T>>(std::string_view, Context&)> validator;
    std::function<void(Context&)> on_unauthorized{nullptr};
    bool abort_on_failure{true};
};

template <typename T>
struct CookieAuthOptions {
    std::function<core::Task<std::optional<T>>(std::string_view, Context&)> validator;
    std::function<void(Context&)> on_unauthorized{nullptr};
    bool abort_on_failure{true};
};

template <typename T>
struct ApiKeyAuthOptions {
    std::function<core::Task<std::optional<T>>(std::string_view, Context&)> validator;
    std::function<void(Context&)> on_unauthorized{nullptr};
    bool abort_on_failure{true};
};

template <typename T>
struct BasicAuthOptions {
    std::function<core::Task<std::optional<T>>(std::string_view, std::string_view, Context&)> validator;
    std::string realm{"Access"};
    std::function<void(Context&)> on_unauthorized{nullptr};
    bool abort_on_failure{true};
};

// -----------------------------------------------------------------------------
// Middleware Factories
// -----------------------------------------------------------------------------

/**
 * @brief HTTP Bearer Token authentication middleware factory.
 *
 * Extracts the token from the "Authorization: Bearer <token>" header, passes it to the validator,
 * and sets the resolved principal into ctx.set<T>(principal).
 */
template <typename T>
inline MiddlewareFn bearer_auth(BearerAuthOptions<T> options) {
    return [opts = std::move(options)](Context& ctx, Next next) -> core::Task<void> {
        auto auth_header = ctx.req().header("authorization");
        if (!auth_header.has_value()) {
            if (opts.on_unauthorized) opts.on_unauthorized(ctx);
            else detail::default_send_unauthorized(ctx, "Authentication credentials are missing or invalid.", "Bearer");
            if (opts.abort_on_failure) co_return;
            co_await next(ctx);
            co_return;
        }

        std::string_view header_val = detail::trim_whitespace(*auth_header);
        if (!detail::iequals_prefix(header_val, "bearer ")) {
            if (opts.on_unauthorized) opts.on_unauthorized(ctx);
            else detail::default_send_unauthorized(ctx, "Authentication scheme must be Bearer.", "Bearer");
            if (opts.abort_on_failure) co_return;
            co_await next(ctx);
            co_return;
        }

        std::string_view token = detail::trim_whitespace(header_val.substr(7));
        if (token.empty()) {
            if (opts.on_unauthorized) opts.on_unauthorized(ctx);
            else detail::default_send_unauthorized(ctx, "Bearer token is empty.", "Bearer");
            if (opts.abort_on_failure) co_return;
            co_await next(ctx);
            co_return;
        }

        auto principal = co_await opts.validator(token, ctx);
        if (!principal.has_value()) {
            if (opts.on_unauthorized) opts.on_unauthorized(ctx);
            else detail::default_send_unauthorized(ctx, "Authentication credentials are missing or invalid.", "Bearer");
            if (opts.abort_on_failure) co_return;
            co_await next(ctx);
            co_return;
        }

        ctx.set<T>(std::move(*principal));
        co_await next(ctx);
    };
}

/**
 * @brief Convenience overload for bearer_auth directly accepting a JwtVerifier.
 *
 * Verifies the bearer JWT and automatically sets ctx.set<T>(res->claims) on success.
 *
 * Example:
 * @code
 * jwt::JwtVerifier<UserClaims> verifier(jwt::Algorithm::HS256, "secret");
 * app.use(bearer_auth(verifier));
 * @endcode
 */
template <typename T = void, typename TDefault = void>
inline MiddlewareFn bearer_auth(jwt::JwtVerifier<TDefault> verifier, std::function<void(Context&)> on_unauthorized = nullptr) {
    using TargetType = std::conditional_t<std::is_void_v<T>, TDefault, T>;
    static_assert(!std::is_void_v<TargetType>, "bearer_auth requires a non-void claims type. Specify bearer_auth<MyClaims>(verifier) or use JwtVerifier<MyClaims>.");
    return bearer_auth<TargetType>({
        .validator = [verifier = std::move(verifier)](std::string_view token, Context&) -> core::Task<std::optional<TargetType>> {
            auto res = verifier.template verify<TargetType>(token);
            if (!res.has_value()) co_return std::nullopt;
            co_return res->claims;
        },
        .on_unauthorized = std::move(on_unauthorized)
    });
}

/**
 * @brief Cookie-based authentication middleware factory (session cookies).
 *
 * Extracts session identifier or JWT from ctx.req().cookie(cookie_name).
 */
template <typename T>
inline MiddlewareFn cookie_auth(std::string cookie_name, CookieAuthOptions<T> options) {
    return [name = std::move(cookie_name), opts = std::move(options)](Context& ctx, Next next) -> core::Task<void> {
        auto cookie_val = ctx.req().cookie(name);
        if (!cookie_val.has_value() || cookie_val->empty()) {
            if (opts.on_unauthorized) opts.on_unauthorized(ctx);
            else detail::default_send_unauthorized(ctx, "Authentication session cookie is missing or invalid.");
            if (opts.abort_on_failure) co_return;
            co_await next(ctx);
            co_return;
        }

        auto principal = co_await opts.validator(*cookie_val, ctx);
        if (!principal.has_value()) {
            if (opts.on_unauthorized) opts.on_unauthorized(ctx);
            else detail::default_send_unauthorized(ctx, "Authentication session cookie is missing or invalid.");
            if (opts.abort_on_failure) co_return;
            co_await next(ctx);
            co_return;
        }

        ctx.set<T>(std::move(*principal));
        co_await next(ctx);
    };
}

/**
 * @brief Convenience overload for cookie_auth directly accepting a JwtVerifier.
 *
 * Verifies the cookie JWT and automatically sets ctx.set<T>(res->claims) on success.
 *
 * Example:
 * @code
 * jwt::JwtVerifier<UserClaims> verifier(jwt::Algorithm::HS256, "secret");
 * app.use(cookie_auth("session", verifier));
 * @endcode
 */
template <typename T = void, typename TDefault = void>
inline MiddlewareFn cookie_auth(std::string cookie_name, jwt::JwtVerifier<TDefault> verifier, std::function<void(Context&)> on_unauthorized = nullptr) {
    using TargetType = std::conditional_t<std::is_void_v<T>, TDefault, T>;
    static_assert(!std::is_void_v<TargetType>, "cookie_auth requires a non-void claims type. Specify cookie_auth<MyClaims>(cookie_name, verifier) or use JwtVerifier<MyClaims>.");
    return cookie_auth<TargetType>(std::move(cookie_name), {
        .validator = [verifier = std::move(verifier)](std::string_view token, Context&) -> core::Task<std::optional<TargetType>> {
            auto res = verifier.template verify<TargetType>(token);
            if (!res.has_value()) co_return std::nullopt;
            co_return res->claims;
        },
        .on_unauthorized = std::move(on_unauthorized)
    });
}

/**
 * @brief API Key authentication middleware factory.
 *
 * Extracts API key from request header (e.g. "X-API-Key").
 */
template <typename T>
inline MiddlewareFn api_key_auth(std::string header_name, ApiKeyAuthOptions<T> options) {
    return [header = std::move(header_name), opts = std::move(options)](Context& ctx, Next next) -> core::Task<void> {
        auto key_val = ctx.req().header(header);
        if (!key_val.has_value() || key_val->empty()) {
            if (opts.on_unauthorized) opts.on_unauthorized(ctx);
            else detail::default_send_unauthorized(ctx, "API key is missing or invalid.");
            if (opts.abort_on_failure) co_return;
            co_await next(ctx);
            co_return;
        }

        auto principal = co_await opts.validator(*key_val, ctx);
        if (!principal.has_value()) {
            if (opts.on_unauthorized) opts.on_unauthorized(ctx);
            else detail::default_send_unauthorized(ctx, "API key is missing or invalid.");
            if (opts.abort_on_failure) co_return;
            co_await next(ctx);
            co_return;
        }

        ctx.set<T>(std::move(*principal));
        co_await next(ctx);
    };
}

/**
 * @brief HTTP Basic authentication middleware factory (RFC 7617).
 *
 * Extracts "Authorization: Basic <base64>", decodes user:password, and calls validator.
 */
template <typename T>
inline MiddlewareFn basic_auth(BasicAuthOptions<T> options) {
    std::string challenge = "Basic realm=\"" + options.realm + "\"";
    return [opts = std::move(options), chal = std::move(challenge)](Context& ctx, Next next) -> core::Task<void> {
        auto auth_header = ctx.req().header("authorization");
        if (!auth_header.has_value()) {
            if (opts.on_unauthorized) opts.on_unauthorized(ctx);
            else detail::default_send_unauthorized(ctx, "Authentication credentials are missing or invalid.", chal);
            if (opts.abort_on_failure) co_return;
            co_await next(ctx);
            co_return;
        }

        std::string_view header_val = detail::trim_whitespace(*auth_header);
        if (!detail::iequals_prefix(header_val, "basic ")) {
            if (opts.on_unauthorized) opts.on_unauthorized(ctx);
            else detail::default_send_unauthorized(ctx, "Authentication scheme must be Basic.", chal);
            if (opts.abort_on_failure) co_return;
            co_await next(ctx);
            co_return;
        }

        std::string_view b64 = detail::trim_whitespace(header_val.substr(6));
        auto decoded = jwt::base64_decode(b64);
        if (!decoded.has_value()) {
            if (opts.on_unauthorized) opts.on_unauthorized(ctx);
            else detail::default_send_unauthorized(ctx, "Invalid Base64 encoding for Basic credentials.", chal);
            if (opts.abort_on_failure) co_return;
            co_await next(ctx);
            co_return;
        }

        size_t colon_pos = decoded->find(':');
        if (colon_pos == std::string::npos) {
            if (opts.on_unauthorized) opts.on_unauthorized(ctx);
            else detail::default_send_unauthorized(ctx, "Malformed Basic credentials: missing colon delimiter.", chal);
            if (opts.abort_on_failure) co_return;
            co_await next(ctx);
            co_return;
        }

        std::string_view username = std::string_view(*decoded).substr(0, colon_pos);
        std::string_view password = std::string_view(*decoded).substr(colon_pos + 1);

        auto principal = co_await opts.validator(username, password, ctx);
        if (!principal.has_value()) {
            if (opts.on_unauthorized) opts.on_unauthorized(ctx);
            else detail::default_send_unauthorized(ctx, "Authentication credentials are missing or invalid.", chal);
            if (opts.abort_on_failure) co_return;
            co_await next(ctx);
            co_return;
        }

        ctx.set<T>(std::move(*principal));
        co_await next(ctx);
    };
}

} // namespace aegon::http::middleware
