#include "EventLoop.h"
#include <pthread.h>
#include <sched.h>
#include <stdexcept>
#include <algorithm>

#include <iostream>

namespace aegon::core {

thread_local EventLoop* t_current_loop{nullptr};

EventLoop* EventLoop::current() noexcept {
    return t_current_loop;
}

EventLoop::EventLoop(uint32_t ring_entries, uint16_t pbuf_entries, size_t buffer_size)
    : ring_(ring_entries),
      buffer_pool_(ring_.raw_ring(), DEFAULT_BGID, pbuf_entries, buffer_size) {}

EventLoop::EventLoop(const IoUringConfig& ring_config, uint16_t pbuf_entries, size_t buffer_size)
    : ring_(ring_config),
      buffer_pool_(ring_.raw_ring(), DEFAULT_BGID, pbuf_entries, buffer_size) {}

void EventLoop::pin_to_core(int core_id) {
    if (core_id < 0 || core_id >= CPU_SETSIZE) {
        throw std::invalid_argument("Invalid core_id: " + std::to_string(core_id));
    }
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        throw std::system_error(rc, std::generic_category(), "pthread_setaffinity_np failed");
    }
}

void EventLoop::spawn(Task<void> task) {
    t_current_loop = this;
    task.resume();
    if (!task.is_ready()) {
        tasks_.push_back(std::move(task));
    }
}

void EventLoop::stop() noexcept {
    running_.store(false, std::memory_order_release);
    // Wake up thread if currently sleeping in submit_and_wait(1)
    if (ring_.raw_ring()) {
        struct io_uring_sqe* sqe = ring_.acquire_sqe();
        if (sqe) {
            io_uring_prep_nop(sqe);
            io_uring_sqe_set_data(sqe, nullptr);
            (void)io_uring_submit(ring_.raw_ring());
        }
    }
}

void EventLoop::run() {
    struct LoopGuard {
        EventLoop* prev;
        explicit LoopGuard(EventLoop* cur) noexcept : prev(t_current_loop) {
            t_current_loop = cur;
        }
        ~LoopGuard() {
            t_current_loop = prev;
        }
    } guard(this);

    running_.store(true, std::memory_order_release);

    while (running_.load(std::memory_order_acquire)) {
        // Clean up completed tasks and log any uncaught exceptions
        std::erase_if(tasks_, [](Task<void>& t) {
            if (t.is_ready()) {
                try {
                    t.result();
                } catch (const std::exception& e) {
                    std::cerr << "[EventLoop] Uncaught exception in root task: " << e.what() << '\n';
                } catch (...) {
                    std::cerr << "[EventLoop] Uncaught unknown exception in root task\n";
                }
                return true;
            }
            return false;
        });

        // If no more tasks and no pending I/O, exit
        if (tasks_.empty()) {
            break;
        }

        // Wait for at least one completion event
        ring_.submit_and_wait(1);

        // Process all events and resume coroutines
        ring_.process_completions();
    }
}

} // namespace aegon::core
