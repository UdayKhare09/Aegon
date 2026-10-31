#include "core/ThreadPool.h"
#include <algorithm>

namespace aegon::core {

ThreadPool::ThreadPool(size_t threads) {
    size_t count = threads > 0 ? threads : std::max(2u, std::thread::hardware_concurrency());
    workers_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        workers_.emplace_back([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->mutex_);
                    this->cv_.wait(lock, [this]() {
                        return !this->running_ || !this->tasks_.empty();
                    });

                    if (!this->running_ && this->tasks_.empty()) {
                        return;
                    }

                    task = std::move(this->tasks_.front());
                    this->tasks_.pop();
                }

                if (task) {
                    try {
                        task();
                    } catch (...) {
                        // Suppress background thread exceptions
                    }
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::post(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!running_) return;
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

void ThreadPool::stop() {
    bool expected = true;
    if (running_.compare_exchange_strong(expected, false)) {
        cv_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
}

ThreadPool& ThreadPool::default_pool() {
    static ThreadPool s_pool(std::max(2u, std::min(8u, std::thread::hardware_concurrency())));
    return s_pool;
}

} // namespace aegon::core

