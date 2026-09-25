#include "EventLoop.h"
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <poll.h>
#include <stdexcept>
#include <algorithm>
#include <iostream>

namespace aegon::core {

thread_local EventLoop* t_current_loop{nullptr};

EventLoop* EventLoop::current() noexcept {
    return t_current_loop;
}

// ---------------------------------------------------------------------------
// Constructors / Destructor
// ---------------------------------------------------------------------------

EventLoop::EventLoop(uint32_t ring_entries, uint16_t pbuf_entries, size_t buffer_size)
    : ring_(ring_entries),
      buffer_pool_(ring_.raw_ring(), DEFAULT_BGID, pbuf_entries, buffer_size),
      wakeup_awaiter_(*this) {
    wakeup_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    // Non-fatal: if eventfd fails, stop() degrades to a non-waking flag-only stop
}

EventLoop::EventLoop(const IoUringConfig& ring_config, uint16_t pbuf_entries, size_t buffer_size)
    : ring_(ring_config),
      buffer_pool_(ring_.raw_ring(), DEFAULT_BGID, pbuf_entries, buffer_size),
      wakeup_awaiter_(*this) {
    wakeup_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
}

EventLoop::~EventLoop() {
    if (wakeup_fd_ >= 0) {
        ::close(wakeup_fd_);
        wakeup_fd_ = -1;
    }
}

// ---------------------------------------------------------------------------
// WakeupAwaiter — polls wakeup_fd_ from inside the ring
// ---------------------------------------------------------------------------

void EventLoop::WakeupAwaiter::submit() noexcept {
    if (loop.wakeup_fd_ < 0) return;
    struct io_uring_sqe* sqe = loop.ring_.acquire_sqe();
    if (!sqe) [[unlikely]] return;
    io_uring_prep_poll_add(sqe, loop.wakeup_fd_, POLLIN);
    io_uring_sqe_set_data(sqe, this);
}

void EventLoop::WakeupAwaiter::on_completion(int res, uint32_t /*flags*/) noexcept {
    if (res > 0 && loop.wakeup_fd_ >= 0) {
        uint64_t v;
        (void)::read(loop.wakeup_fd_, &v, sizeof(v)); // drain
    }
    loop.running_.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// pin_to_core
// ---------------------------------------------------------------------------

void EventLoop::pin_to_core(int core_id) {
    if (core_id < 0 || core_id >= CPU_SETSIZE) {
        throw std::invalid_argument("Invalid core_id: " + std::to_string(core_id));
    }
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        throw std::system_error(rc, std::generic_category(),
                                "pthread_setaffinity_np failed");
    }
}

// ---------------------------------------------------------------------------
// spawn — enqueue a root task
// ---------------------------------------------------------------------------

void EventLoop::spawn(Task<void> task) {
    // Do NOT set t_current_loop here — that is done exclusively by the LoopGuard
    // in run(). Setting it from spawn() would expose an inconsistent state where
    // current() is valid but is_running() is still false.
    task.resume();
    if (!task.is_ready()) {
        tasks_.push_back(std::move(task));
    }
}

// ---------------------------------------------------------------------------
// stop — thread-safe via eventfd
// ---------------------------------------------------------------------------

void EventLoop::stop() noexcept {
    running_.store(false, std::memory_order_release);
    if (wakeup_fd_ >= 0) {
        uint64_t one = 1;
        // Plain write() is thread-safe and does NOT touch the io_uring SQ.
        // This unblocks submit_and_wait() by making the poll CQE fire.
        (void)::write(wakeup_fd_, &one, sizeof(one));
    }
}

// ---------------------------------------------------------------------------
// run — the main event loop
// ---------------------------------------------------------------------------

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

    // Arm the cross-thread wakeup poll. When stop() writes to wakeup_fd_,
    // submit_and_wait() returns with a POLLIN CQE, process_completions() calls
    // WakeupAwaiter::on_completion(), which sets running_ = false.
    wakeup_awaiter_.submit();

    while (running_.load(std::memory_order_acquire)) {
        // Submit all pending SQEs and wait for at least one CQE.
        // With IORING_SETUP_DEFER_TASKRUN this also flushes deferred task work.
        ring_.submit_and_wait(1);

        // Process all ready CQEs and resume awaiting coroutines.
        // May set running_ = false via WakeupAwaiter::on_completion().
        ring_.process_completions();

        // Flush any batched BufferPool returns accumulated during CQE processing.
        buffer_pool_.flush_returns();

        // Cleanup completed root tasks and log uncaught exceptions.
        std::erase_if(tasks_, [](Task<void>& t) {
            if (t.is_ready()) {
                try {
                    t.result();
                } catch (const std::exception& e) {
                    std::cerr << "[EventLoop] Uncaught exception in root task: "
                              << e.what() << '\n';
                } catch (...) {
                    std::cerr << "[EventLoop] Uncaught unknown exception in root task\n";
                }
                return true;
            }
            return false;
        });

        // Auto-exit: all tasks completed AND the ring has no pending SQEs or CQEs.
        // This accounts for multishot operations whose CQEs might still arrive
        // after the root task finishes, so we check ring idle explicitly.
        if (tasks_.empty() &&
            io_uring_sq_ready(ring_.raw_ring()) == 0 &&
            io_uring_cq_ready(ring_.raw_ring()) == 0) {
            break;
        }
    }

    running_.store(false, std::memory_order_release);
}

} // namespace aegon::core
