#pragma once

#include "http/Context.h"
#include "core/Task.h"
#include <functional>
#include <span>
#include <vector>
#include <type_traits>
#include <concepts>
#include <utility>

namespace aegon::http {

using Next = std::function<core::Task<void>(Context&)>;
using MiddlewareFn = std::function<core::Task<void>(Context&, Next)>;
using Handler = Next;

/**
 * @brief Factory converting various callable forms into standard MiddlewareFn.
 *
 * Supported signatures:
 * 1. Task<void>(Context&, Next)  - standard async onion middleware
 * 2. void(Context&, Next)        - sync middleware receiving Next
 * 3. Task<void>(Context&)        - async pre-handler middleware (auto calls next)
 * 4. void(Context&)              - sync pre-handler middleware (auto calls next)
 */
template <typename F>
inline MiddlewareFn make_middleware(F&& f) {
    if constexpr (std::is_invocable_r_v<core::Task<void>, F, Context&, Next>) {
        return std::forward<F>(f);
    } else if constexpr (std::is_invocable_r_v<void, F, Context&, Next>) {
        return [func = std::forward<F>(f)](Context& ctx, Next next) -> core::Task<void> {
            func(ctx, next);
            co_return;
        };
    } else if constexpr (std::is_invocable_r_v<core::Task<void>, F, Context&>) {
        return [func = std::forward<F>(f)](Context& ctx, Next next) -> core::Task<void> {
            co_await func(ctx);
            co_await next(ctx);
        };
    } else if constexpr (std::is_invocable_r_v<void, F, Context&>) {
        return [func = std::forward<F>(f)](Context& ctx, Next next) -> core::Task<void> {
            func(ctx);
            co_await next(ctx);
        };
    } else {
        static_assert(sizeof(F) == 0,
            "Middleware must be callable as (Context&, Next) returning Task<void> or (Context&) returning void/Task<void>");
    }
}

/**
 * @brief Executes a middleware chain across global and local spans with zero per-request allocation.
 *
 * Traverses global middlewares first, then local (group + per-route) middlewares, and finally the terminal handler.
 * If any middleware does not invoke next(ctx), execution terminates early (short-circuit).
 */
inline core::Task<void> run_chain(
    std::span<const MiddlewareFn> global,
    std::span<const MiddlewareFn> local,
    size_t idx,
    const Handler& final_handler,
    Context& ctx)
{
    const size_t total = global.size() + local.size();
    if (idx < total) {
        const auto& mw = (idx < global.size()) ? global[idx] : local[idx - global.size()];
        Next next = [global, local, idx, &final_handler](Context& c) -> core::Task<void> {
            co_await run_chain(global, local, idx + 1, final_handler, c);
        };
        co_await mw(ctx, std::move(next));
    } else {
        if (final_handler) {
            co_await final_handler(ctx);
        }
    }
}

} // namespace aegon::http
