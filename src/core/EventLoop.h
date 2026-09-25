#pragma once

#include "IoUring.h"
#include "BufferPool.h"
#include "Task.h"
#include <vector>
#include <functional>
#include <atomic>

namespace aegon::core {

/**
 * @brief Thread-per-core event loop combining io_uring and the Provided Buffer Ring.
 *
 * Runs without cross-thread locks. Each worker core runs its own EventLoop.
 *
 * Cross-thread stop:
 *   stop() is safe to call from any thread. It writes to an eventfd (no ring
 *   access from a foreign thread) which unblocks the loop's submit_and_wait()
 *   call. This is required because IORING_SETUP_SINGLE_ISSUER makes any SQ
 *   write from a non-owner thread undefined behavior.
 *
 * Auto-exit:
 *   When all spawned tasks complete AND the ring has no unsubmitted SQEs or
 *   unprocessed CQEs, the loop exits even without an explicit stop() call.
 */
class EventLoop {
public:
    static constexpr uint16_t DEFAULT_BGID = 1;

    explicit EventLoop(uint32_t ring_entries = 4096,
                       uint16_t pbuf_entries = 2048,
                       size_t buffer_size = 4096);

    explicit EventLoop(const IoUringConfig& ring_config,
                       uint16_t pbuf_entries = 2048,
                       size_t buffer_size = 4096);

    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

    // Start the event loop (blocks until stop() or all tasks finish)
    void run();

    /**
     * Request the loop to stop. Thread-safe — does NOT touch the io_uring SQ.
     * Uses an eventfd write to unblock submit_and_wait() from any thread.
     */
    void stop() noexcept;

    // Pin this event loop's thread to a specific CPU core
    static void pin_to_core(int core_id);

    // Get current thread's active EventLoop (only valid during run())
    static EventLoop* current() noexcept;

    /**
     * Spawn a root coroutine Task onto the event loop.
     * Tasks queued before run() are started on the first run() iteration.
     * Tasks queued during run() (from within a coroutine) are started immediately.
     */
    void spawn(Task<void> task);

    [[nodiscard]] IoUring&     ring()        noexcept { return ring_; }
    [[nodiscard]] BufferPool&  buffer_pool() noexcept { return buffer_pool_; }
    [[nodiscard]] bool is_running() const noexcept {
        return running_.load(std::memory_order_relaxed);
    }

private:
    /**
     * Internal awaiter that polls the wakeup eventfd.
     * When stop() writes to the eventfd, this awaiter's on_completion() fires
     * and sets running_ = false — entirely from within the event loop thread.
     */
    struct WakeupAwaiter : IoAwaiter {
        EventLoop& loop;
        explicit WakeupAwaiter(EventLoop& l) noexcept : loop(l) {}
        void submit() noexcept override;
        void on_completion(int res, uint32_t flags) noexcept override;
    };

    IoUring     ring_;
    BufferPool  buffer_pool_;
    std::atomic<bool> running_{false};
    std::vector<Task<void>> tasks_;
    int         wakeup_fd_{-1};    // eventfd for cross-thread stop()
    WakeupAwaiter wakeup_awaiter_; // polls wakeup_fd_ while the loop runs
};

} // namespace aegon::core
