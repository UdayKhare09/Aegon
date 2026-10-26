#pragma once

#include "core/Task.h"
#include <functional>
#include <utility>
#include <cstdint>
#include <cassert>

namespace aegon::http {

class Context;

using SyncHandler = std::function<void(Context&)>;
using AsyncHandler = std::function<core::Task<void>(Context&)>;

/**
 * @brief High-performance dual-mode route handler supporting both synchronous functions
 *        and asynchronous C++20 coroutines.
 *
 * Synchronous handlers (void(Context&)) execute inline on the worker/reactor thread with
 * ZERO coroutine allocations, ZERO task frames, and ZERO suspension/resumption transitions.
 *
 * Asynchronous handlers (Task<void>(Context&)) execute as C++20 coroutines for asynchronous I/O.
 */
class RouteHandler {
public:
    enum class Kind : uint8_t { None, Sync, Async };

private:
    Kind kind_{Kind::None};
    bool is_streaming_{false};
    SyncHandler sync_fn_{nullptr};
    AsyncHandler async_fn_{nullptr};

public:
    RouteHandler() = default;
    /* implicit */ RouteHandler(std::nullptr_t) noexcept : kind_(Kind::None) {}

    /* implicit */ RouteHandler(SyncHandler s)
        : kind_(s ? Kind::Sync : Kind::None), sync_fn_(std::move(s)) {}

    /* implicit */ RouteHandler(AsyncHandler a)
        : kind_(a ? Kind::Async : Kind::None), async_fn_(std::move(a)) {}

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool is_sync() const noexcept { return kind_ == Kind::Sync; }
    [[nodiscard]] bool is_async() const noexcept { return kind_ == Kind::Async; }
    [[nodiscard]] bool is_streaming() const noexcept { return is_streaming_; }
    void set_streaming(bool s) noexcept { is_streaming_ = s; }
    [[nodiscard]] explicit operator bool() const noexcept { return kind_ != Kind::None; }

    [[nodiscard]] const SyncHandler& sync_handler() const noexcept { return sync_fn_; }
    [[nodiscard]] const AsyncHandler& async_handler() const noexcept { return async_fn_; }

    void invoke_sync(Context& ctx) const {
        assert(kind_ == Kind::Sync && sync_fn_ != nullptr);
        sync_fn_(ctx);
    }

    core::Task<void> operator()(Context& ctx) const {
        if (kind_ == Kind::Sync) {
            sync_fn_(ctx);
            co_return;
        } else if (kind_ == Kind::Async) {
            co_await async_fn_(ctx);
        }
    }
};

using Handler = RouteHandler;

} // namespace aegon::http
