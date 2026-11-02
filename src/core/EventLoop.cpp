#include "core/EventLoop.h"
#include <cstdio>
#include <stdexcept>
#include <system_error>

#include <pthread.h>
#include <sched.h>
#include <unistd.h>

namespace aegon::core {

thread_local EventLoop* t_current_loop{nullptr};

EventLoop* EventLoop::current() noexcept {
    return t_current_loop;
}

EventLoop::EventLoop(const EngineConfig& config, uint16_t pbuf_entries, size_t buffer_size)
    : buffer_pool_(DEFAULT_BGID, pbuf_entries, buffer_size),
      engine_(IoEngine::create(config, buffer_pool_)) {
}

EventLoop::EventLoop(uint32_t entries, uint16_t pbuf_entries, size_t buffer_size)
    : buffer_pool_(DEFAULT_BGID, pbuf_entries, buffer_size) {
    EngineConfig cfg;
    cfg.entries = entries;
    engine_ = IoEngine::create(cfg, buffer_pool_);
}

EventLoop::~EventLoop() {
    stop();
    drain_post_queue();
    if (engine_) {
        engine_->process_completions();
    }
    tasks_.clear();
}

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
    task.resume();
    if (!task.is_ready()) {
        tasks_.push_back(std::move(task));
    }
}

void EventLoop::post(std::function<void()> fn) {
    if (!fn) return;
    {
        std::lock_guard<std::mutex> lock(post_mutex_);
        post_queue_.push_back(std::move(fn));
    }
    if (engine_) {
        engine_->wakeup();
    }
}

void EventLoop::drain_post_queue() {
    std::vector<std::function<void()>> local_queue;
    {
        std::lock_guard<std::mutex> lock(post_mutex_);
        if (post_queue_.empty()) return;
        local_queue.swap(post_queue_);
    }
    for (auto& fn : local_queue) {
        try {
            fn();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[EventLoop] Uncaught exception in posted task: %s\n", e.what());
        } catch (...) {
            std::fprintf(stderr, "[EventLoop] Uncaught unknown exception in posted task\n");
        }
    }
}

void EventLoop::stop() noexcept {
    running_.store(false, std::memory_order_release);
    if (engine_) {
        engine_->wakeup();
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
        drain_post_queue();
        if (tasks_.empty()) {
            break;
        }

        engine_->submit_and_wait(1);
        engine_->process_completions();
        drain_post_queue();
        buffer_pool_.flush_returns();

        std::erase_if(tasks_, [](Task<void>& t) {
            if (t.is_ready()) {
                try {
                    t.result();
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "[EventLoop] Uncaught exception in root task: %s\n", e.what());
                } catch (...) {
                    std::fprintf(stderr, "[EventLoop] Uncaught unknown exception in root task\n");
                }
                return true;
            }
            return false;
        });

        if (tasks_.empty()) {
            break;
        }
    }

    drain_post_queue();
    if (engine_) {
        engine_->process_completions();
    }

    running_.store(false, std::memory_order_release);
}

} // namespace aegon::core
