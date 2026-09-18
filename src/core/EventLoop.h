#pragma once

#include "IoUring.h"
#include "BufferPool.h"
#include "Task.h"
#include <vector>
#include <functional>

namespace aegon::core {

/**
 * @brief Thread-per-core event loop combining io_uring and the Provided Buffer Ring.
 *
 * Runs without cross-thread locks. Each worker core runs its own EventLoop.
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

    ~EventLoop() = default;

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

    // Start the event loop
    void run();

    // Request the loop to stop
    void stop() noexcept;

    // Pin this event loop thread to a specific CPU core
    static void pin_to_core(int core_id);

    // Get current thread's active EventLoop (if running)
    static EventLoop* current() noexcept;

    // Spawn a root coroutine Task onto the event loop
    void spawn(Task<void> task);

    [[nodiscard]] IoUring& ring() noexcept { return ring_; }
    [[nodiscard]] BufferPool& buffer_pool() noexcept { return buffer_pool_; }
    [[nodiscard]] bool is_running() const noexcept { return running_; }

private:
    IoUring ring_;
    BufferPool buffer_pool_;
    bool running_{false};
    std::vector<Task<void>> tasks_;
};

} // namespace aegon::core
