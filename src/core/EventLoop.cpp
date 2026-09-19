#include "EventLoop.h"
#include <pthread.h>
#include <sched.h>
#include <stdexcept>
#include <algorithm>
#include <cerrno>

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
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        throw std::system_error(rc, std::generic_category(), "pthread_setaffinity_np failed");
    }
}

std::vector<int> EventLoop::available_cpus() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    if (sched_getaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        throw std::system_error(errno, std::generic_category(), "sched_getaffinity failed");
    }

    std::vector<int> cpus;
    cpus.reserve(CPU_COUNT(&cpuset));
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &cpuset)) {
            cpus.push_back(cpu);
        }
    }
    return cpus;
}

void EventLoop::spawn(Task<void> task) {
    t_current_loop = this;
    task.resume();
    if (!task.is_ready()) {
        tasks_.push_back(std::move(task));
    }
}

void EventLoop::stop() noexcept {
    running_ = false;
}

void EventLoop::run() {
    t_current_loop = this;
    running_ = true;

    while (running_) {
        // Clean up completed tasks
        std::erase_if(tasks_, [](const Task<void>& t) { return t.is_ready(); });

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
