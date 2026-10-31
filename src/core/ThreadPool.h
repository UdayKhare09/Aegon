#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <atomic>

namespace aegon::core {

/**
 * @brief High-performance worker thread pool for background and asynchronous storage I/O.
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t threads = 4);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /**
     * @brief Post a void task for asynchronous execution.
     */
    void post(std::function<void()> task);

    /**
     * @brief Dispatch a callable and receive a future with the return value.
     */
    template <typename F, typename... Args>
    auto dispatch(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [f = std::forward<F>(f), ...args = std::forward<Args>(args)]() mutable {
                return std::invoke(f, std::forward<Args>(args)...);
            }
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!running_) {
                throw std::runtime_error("ThreadPool has been stopped");
            }
            tasks_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();
        return res;
    }

    /**
     * @brief Stops accepting new tasks and waits for active workers to complete.
     */
    void stop();

    [[nodiscard]] size_t size() const noexcept { return workers_.size(); }
    [[nodiscard]] bool is_running() const noexcept { return running_; }

    /**
     * @brief Global default shared thread pool for I/O engine storage fallbacks.
     */
    static ThreadPool& default_pool();

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{true};
};

} // namespace aegon::core

