#include "core/EventLoop.h"
#include "log/Logger.h"
#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

namespace aegon::core {

thread_local EventLoop* t_current_loop{nullptr};

EventLoop* EventLoop::current() noexcept {
    return t_current_loop;
}

EventLoop::EventLoop(const EngineConfig& config, uint16_t pbuf_entries, size_t buffer_size)
    : buffer_pool_(DEFAULT_BGID, pbuf_entries, buffer_size),
      engine_(IoEngine::create(config, buffer_pool_)) {
    log::debug("[EventLoop] Created event loop: engine={}, pbuf_entries={}, buf_size={}",
               engine_->name(), pbuf_entries, buffer_size);
}

EventLoop::EventLoop(uint32_t entries, uint16_t pbuf_entries, size_t buffer_size)
    : buffer_pool_(DEFAULT_BGID, pbuf_entries, buffer_size) {
    EngineConfig cfg;
    cfg.entries = entries;
    engine_ = IoEngine::create(cfg, buffer_pool_);
    log::debug("[EventLoop] Created event loop: engine={}, entries={}, pbuf_entries={}, buf_size={}",
               engine_->name(), entries, pbuf_entries, buffer_size);
}

EventLoop::~EventLoop() {
    stop();
    if (engine_) {
        engine_->process_completions();
    }
    tasks_.clear();
}

void EventLoop::pin_to_core(int core_id) {
#if defined(__linux__)
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
    log::debug("[EventLoop] Pinned thread to CPU core {}", core_id);
#elif defined(_WIN32)
    if (core_id < 0 || core_id >= 64) {
        throw std::invalid_argument("Invalid core_id: " + std::to_string(core_id));
    }
    DWORD_PTR mask = static_cast<DWORD_PTR>(1) << core_id;
    if (SetThreadAffinityMask(GetCurrentThread(), mask) == 0) {
        DWORD err = GetLastError();
        throw std::system_error(static_cast<int>(err), std::system_category(), "SetThreadAffinityMask failed");
    }
    log::debug("[EventLoop] Pinned thread to CPU core {}", core_id);
#endif
}

void EventLoop::spawn(Task<void> task) {
    task.resume();
    if (!task.is_ready()) {
        tasks_.push_back(std::move(task));
    }
}

void EventLoop::stop() noexcept {
    log::debug("[EventLoop] Stop requested");
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
    log::debug("[EventLoop] Starting event loop (engine={}, root tasks={})",
               engine_->name(), tasks_.size());

    while (running_.load(std::memory_order_acquire)) {
        if (tasks_.empty()) {
            break;
        }

        engine_->submit_and_wait(1);
        engine_->process_completions();
        buffer_pool_.flush_returns();

        std::erase_if(tasks_, [](Task<void>& t) {
            if (t.is_ready()) {
                try {
                    t.result();
                } catch (const std::exception& e) {
                    log::error("[EventLoop] Uncaught exception in root task: {}", e.what());
                } catch (...) {
                    log::error("[EventLoop] Uncaught unknown exception in root task");
                }
                return true;
            }
            return false;
        });

        if (tasks_.empty()) {
            break;
        }
    }

    if (engine_) {
        engine_->process_completions();
    }

    running_.store(false, std::memory_order_release);
    log::debug("[EventLoop] Event loop finished");
}

} // namespace aegon::core
