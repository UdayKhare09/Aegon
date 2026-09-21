#pragma once

#include "MemStore.h"
#include "data/cache/CacheBackend.h"
#include "core/EventLoop.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <stdexcept>

namespace aegon::data::memory {

/**
 * @brief CacheBackend implementation backed by MemStore.
 *
 * Drop-in replacement for RedisCacheBackend when running a single-node setup
 * or in testing. All ops are fully async: the HTTP worker coroutine suspends on
 * an io_uring eventfd read while the MemStore thread processes the request,
 * then resumes with the result — zero blocking of the event loop.
 *
 * Usage:
 * @code
 *   auto store = std::make_shared<MemStore>();
 *   store->start();
 *   server.provide<MemStore>(store);
 *
 *   // In on_start or per-handler:
 *   auto cache = std::make_shared<MemStoreCacheBackend>(store);
 *   db->set_cache(cache);
 * @endcode
 */
class MemStoreCacheBackend : public cache::CacheBackend {
public:
    explicit MemStoreCacheBackend(std::shared_ptr<MemStore> store)
        : store_(std::move(store)) {
        if (!store_) {
            throw std::invalid_argument("MemStoreCacheBackend: store must not be null");
        }
    }

    // ── CacheBackend interface ─────────────────────────────────────────────

    core::Task<std::optional<std::string>> get(std::string_view key) override {
        co_return co_await run_op([&](MemRequest& req) {
            req.op  = MemOp::Get;
            req.key = std::string(key);
        }, [](MemRequest& req) -> std::optional<std::string> {
            return std::move(req.result_str);
        });
    }

    core::Task<std::vector<std::optional<std::string>>>
    mget(const std::vector<std::string>& keys) override {
        co_return co_await run_op([&](MemRequest& req) {
            req.op   = MemOp::MGet;
            req.keys = keys;
        }, [](MemRequest& req) -> std::vector<std::optional<std::string>> {
            return std::move(req.result_mstr);
        });
    }

    core::Task<bool> set(std::string_view key, std::string_view val,
                         std::optional<std::chrono::seconds> ttl = std::nullopt) override {
        co_return co_await run_op([&](MemRequest& req) {
            req.op    = MemOp::Set;
            req.key   = std::string(key);
            req.value = std::string(val);
            req.ttl   = ttl;
        }, [](MemRequest& req) -> bool {
            return req.result_bool;
        });
    }

    core::Task<bool> del(std::string_view key) override {
        co_return co_await run_op([&](MemRequest& req) {
            req.op  = MemOp::Del;
            req.key = std::string(key);
        }, [](MemRequest& req) -> bool {
            return req.result_bool;
        });
    }

    core::Task<int64_t> del_many(const std::vector<std::string>& keys) override {
        co_return co_await run_op([&](MemRequest& req) {
            req.op   = MemOp::DelMany;
            req.keys = keys;
        }, [](MemRequest& req) -> int64_t {
            return req.result_i64;
        });
    }

    core::Task<int64_t> incr(std::string_view key) override {
        co_return co_await run_op([&](MemRequest& req) {
            req.op  = MemOp::Incr;
            req.key = std::string(key);
        }, [](MemRequest& req) -> int64_t {
            return req.result_i64;
        });
    }

private:
    /**
     * @brief Helper that:
     *   1. Creates an eventfd.
     *   2. Fills in a MemRequest via setup_fn.
     *   3. Posts to the MemStore thread.
     *   4. co_awaits the eventfd via io_uring (non-blocking suspend).
     *   5. Extracts the result via extract_fn.
     *   6. Closes the eventfd.
     */
    template <typename SetupFn, typename ExtractFn>
    auto run_op(SetupFn&& setup_fn, ExtractFn&& extract_fn)
        -> core::Task<std::invoke_result_t<ExtractFn, MemRequest&>>
    {
        using ResultT = std::invoke_result_t<ExtractFn, MemRequest&>;

        // Create eventfd for this request
        int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (efd < 0) [[unlikely]] {
            co_return ResultT{};
        }

        MemRequest req;
        req.notify_fd = efd;
        setup_fn(req);

        // Get the current thread's io_uring ring
        auto* loop = core::EventLoop::current();
        if (!loop) [[unlikely]] {
            ::close(efd);
            co_return ResultT{};
        }

        // Submit to MemStore thread
        store_->submit(&req);

        // Suspend coroutine until MemStore signals eventfd
        EventFdReadAwaiter awaiter{loop->ring(), efd};
        co_await awaiter;

        ::close(efd);
        co_return extract_fn(req);
    }

    std::shared_ptr<MemStore> store_;
};

} // namespace aegon::data::memory
