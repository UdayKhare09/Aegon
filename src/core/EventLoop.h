#pragma once

#include "core/engine/IoEngine.h"
#include "core/BufferPool.h"
#include "core/Task.h"
#include <vector>
#include <functional>
#include <atomic>
#include <memory>
#include <mutex>

namespace aegon::core {

/**
 * @brief Thread-per-core asynchronous event loop powered by IoEngine and BufferPool.
 *
 * Runs without cross-thread locks. Each worker core executes its own EventLoop.
 *
 * Cross-thread stop:
 *   stop() is thread-safe and safe to call from any thread. It invokes engine().wakeup()
 *   which unblocks the event loop from submit_and_wait().
 *
 * Auto-exit:
 *   When all spawned root tasks complete, the loop exits.
 */
class EventLoop {
public:
    static constexpr uint16_t DEFAULT_BGID = 1;

    explicit EventLoop(const EngineConfig& config = {},
                       uint16_t pbuf_entries = 2048,
                       size_t buffer_size = 4096);

    explicit EventLoop(uint32_t entries,
                       uint16_t pbuf_entries = 2048,
                       size_t buffer_size = 4096);

    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

    // Start the event loop (blocks until stop() or all tasks finish)
    void run();

    // Request the loop to stop. Thread-safe from any thread.
    void stop() noexcept;

    // Pin this event loop's thread to a specific CPU core
    static void pin_to_core(int core_id);

    // Get current thread's active EventLoop (only valid during run())
    static EventLoop* current() noexcept;

    // Spawn a root coroutine Task onto the event loop
    void spawn(Task<void> task);

    // Thread-safe: enqueue fn to run on this event loop as soon as possible.
    void post(std::function<void()> fn);

    [[nodiscard]] IoEngine&   engine()      noexcept { return *engine_; }
    [[nodiscard]] BufferPool& buffer_pool() noexcept { return buffer_pool_; }
    [[nodiscard]] bool is_running() const noexcept {
        return running_.load(std::memory_order_relaxed);
    }

private:
    void drain_post_queue();

    BufferPool                buffer_pool_;
    std::unique_ptr<IoEngine> engine_;
    std::atomic<bool>         running_{false};
    std::vector<Task<void>>   tasks_;
    std::mutex                post_mutex_;
    std::vector<std::function<void()>> post_queue_;
};

struct YieldAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        auto* loop = EventLoop::current();
        if (loop) {
            loop->post([h]() mutable { h.resume(); });
        } else {
            h.resume();
        }
    }
    void await_resume() const noexcept {}
};

/**
 * @brief Cooperatively yields control back to the event loop so other tasks and I/O can run.
 */
[[nodiscard]] inline YieldAwaiter yield() noexcept {
    return YieldAwaiter{};
}

} // namespace aegon::core
