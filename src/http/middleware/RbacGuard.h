#pragma once

#include "http/Context.h"
#include "http/Middleware.h"
#include "http/Protocol.h"
#include "http/ProblemDetails.h"
#include "core/Task.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <concepts>
#include <algorithm>

namespace aegon::http::middleware {

/**
 * @brief Compile-time concept requiring a principal type to expose its assigned roles.
 *
 * Developers' user/session structs must implement:
 *   std::span<const std::string> get_roles() const;
 * or return const std::vector<std::string>& / std::vector<std::string>.
 */
template <typename T>
concept RoleHolder = requires(const T& t) {
    { t.get_roles() } -> std::convertible_to<std::span<const std::string>>;
};

namespace detail {

inline void send_forbidden(Context& ctx, std::string_view detail) {
    ctx.res().status(StatusCode::Forbidden);
    ctx.res().header("Content-Type", "application/problem+json");
    ProblemDetails problem{
        .type = "about:blank",
        .title = "Forbidden",
        .status = 403,
        .detail = std::string(detail)
    };
    ctx.res().body(problem.to_json());
}

inline bool has_role(std::span<const std::string> roles, std::string_view target) noexcept {
    return std::ranges::any_of(roles, [target](const std::string& r) {
        return r == target;
    });
}

} // namespace detail

/**
 * @brief RBAC Guard requiring the principal to hold a specific role.
 *
 * Emits 403 Forbidden with RFC 7807 Problem Details if identity is missing or lacks the role.
 */
template <RoleHolder T>
inline MiddlewareFn require_role(std::string required_role) {
    return [role = std::move(required_role)](Context& ctx, Next next) -> core::Task<void> {
        const T* principal = ctx.get<T>();
        if (!principal) {
            detail::send_forbidden(ctx, "Authentication principal is missing from request context.");
            co_return;
        }

        std::span<const std::string> user_roles = principal->get_roles();
        if (!detail::has_role(user_roles, role)) {
            detail::send_forbidden(ctx, "Access denied: missing required role '" + role + "'.");
            co_return;
        }

        co_await next(ctx);
    };
}

/**
 * @brief RBAC Guard requiring the principal to hold ALL listed roles (AND logic).
 *
 * Emits 403 Forbidden if identity is missing or any role in the list is missing.
 */
template <RoleHolder T>
inline MiddlewareFn require_role(std::vector<std::string> required_roles) {
    return [roles = std::move(required_roles)](Context& ctx, Next next) -> core::Task<void> {
        const T* principal = ctx.get<T>();
        if (!principal) {
            detail::send_forbidden(ctx, "Authentication principal is missing from request context.");
            co_return;
        }

        std::span<const std::string> user_roles = principal->get_roles();
        for (const auto& required : roles) {
            if (!detail::has_role(user_roles, required)) {
                detail::send_forbidden(ctx, "Access denied: missing required role '" + required + "'.");
                co_return;
            }
        }

        co_await next(ctx);
    };
}

/**
 * @brief RBAC Guard requiring the principal to hold AT LEAST ONE of the listed roles (OR logic).
 *
 * Emits 403 Forbidden if identity is missing or none of the listed roles match.
 */
template <RoleHolder T>
inline MiddlewareFn require_any_role(std::vector<std::string> allowed_roles) {
    return [roles = std::move(allowed_roles)](Context& ctx, Next next) -> core::Task<void> {
        const T* principal = ctx.get<T>();
        if (!principal) {
            detail::send_forbidden(ctx, "Authentication principal is missing from request context.");
            co_return;
        }

        std::span<const std::string> user_roles = principal->get_roles();
        bool match = false;
        for (const auto& allowed : roles) {
            if (detail::has_role(user_roles, allowed)) {
                match = true;
                break;
            }
        }

        if (!match) {
            detail::send_forbidden(ctx, "Access denied: requires at least one authorized role.");
            co_return;
        }

        co_await next(ctx);
    };
}

} // namespace aegon::http::middleware
