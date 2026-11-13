#pragma once

#include "http/Context.h"
#include "core/Task.h"
#include <functional>
#include <memory>
#include <span>
#include <vector>
#include <type_traits>
#include <concepts>
#include <utility>
#include "http/RouteHandler.h"

namespace aegon::http {

using Next = std::function<core::Task<void>(Context&)>;

/**
 * @brief Continuation handed to a synchronous middleware: runs the rest of the chain.
 * A non-owning function reference (two pointers), so calling a sync chain allocates nothing.
 */
struct SyncNext {
    void (*fn)(void* self, Context& ctx){nullptr};
    void* self{nullptr};
    void operator()(Context& ctx) const { fn(self, ctx); }
};

/**
 * @brief A middleware: always callable asynchronously as (Context&, Next) -> Task<void>, and
 * optionally also synchronously as (Context&, SyncNext) -> void.
 *
 * When every middleware on a route has a synchronous form and the handler is synchronous,
 * the router composes the whole chain synchronously and the request is served without a
 * coroutine (Router::dispatch_sync). Any callable accepted by make_middleware() converts
 * implicitly, so existing middleware code is unchanged; with_sync() adds the sync form.
 */
class MiddlewareFn {
public:
    using AsyncFn = std::function<core::Task<void>(Context&, Next)>;
    using SyncFn = std::function<void(Context&, SyncNext)>;

    MiddlewareFn() = default;
    MiddlewareFn(std::nullptr_t) noexcept {}
    MiddlewareFn(AsyncFn fn) : async_(std::move(fn)) {}

    template <typename F>
        requires(!std::is_same_v<std::remove_cvref_t<F>, MiddlewareFn> &&
                 !std::is_same_v<std::remove_cvref_t<F>, AsyncFn> &&
                 !std::is_same_v<std::remove_cvref_t<F>, std::nullptr_t> &&
                 std::is_invocable_r_v<core::Task<void>, F, Context&, Next>)
    MiddlewareFn(F&& f) : async_(std::forward<F>(f)) {}

    /// Both forms; they must behave the same.
    static MiddlewareFn with_sync(AsyncFn async_fn, SyncFn sync_fn) {
        MiddlewareFn m(std::move(async_fn));
        m.sync_ = std::move(sync_fn);
        return m;
    }

    core::Task<void> operator()(Context& ctx, Next next) const { return async_(ctx, std::move(next)); }
    explicit operator bool() const noexcept { return static_cast<bool>(async_); }
    [[nodiscard]] bool has_sync() const noexcept { return static_cast<bool>(sync_); }
    [[nodiscard]] const SyncFn& sync() const noexcept { return sync_; }

private:
    AsyncFn async_;
    SyncFn sync_;
};

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
        // A pre-handler step that never waits: it also runs in fully synchronous chains.
        auto shared = std::make_shared<std::decay_t<F>>(std::forward<F>(f));
        return MiddlewareFn::with_sync(
            [shared](Context& ctx, Next next) -> core::Task<void> {
                (*shared)(ctx);
                co_await next(ctx);
            },
            [shared](Context& ctx, SyncNext next) {
                (*shared)(ctx);
                next(ctx);
            });
    } else {
        static_assert(sizeof(F) == 0,
            "Middleware must be callable as (Context&, Next) returning Task<void> or (Context&) returning void/Task<void>");
    }
}

namespace detail {
// Runs chain[i..] synchronously, then the (synchronous) handler. Each step's SyncNext points
// at a stack frame of this runner, so the whole chain allocates nothing.
struct SyncChainStep {
    const MiddlewareFn* chain;
    size_t size;
    size_t index;
    const Handler* handler;

    static void run(void* self, Context& ctx) {
        auto* s = static_cast<SyncChainStep*>(self);
        if (s->index == s->size) {
            s->handler->invoke_sync(ctx);
            return;
        }
        SyncChainStep next{s->chain, s->size, s->index + 1, s->handler};
        s->chain[s->index].sync()(ctx, SyncNext{&SyncChainStep::run, &next});
    }
};
} // namespace detail

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
