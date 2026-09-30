#pragma once

#include "log/Record.h"
#include "log/Sink.h"
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <span>

namespace aegon::log {

class AsyncQueue {
public:
    AsyncQueue(std::vector<SinkPtr> sinks, size_t flush_interval_ms = 10, size_t max_queue_size = 65536)
        : sinks_(std::move(sinks)),
          flush_interval_ms_(flush_interval_ms),
          max_queue_size_(max_queue_size),
          running_(true),
          worker_(&AsyncQueue::worker_loop, this) {}

    ~AsyncQueue() {
        stop();
    }

    void push(Record&& record) {
        bool need_immediate_flush = record.level >= Level::Error;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!running_) return;

            // If queue is overflowing, drop or wait. With 65536 entries, drop oldest to avoid stall
            if (incoming_.size() >= max_queue_size_) {
                incoming_.erase(incoming_.begin(), incoming_.begin() + (max_queue_size_ / 4));
            }
            incoming_.push_back(std::move(record));
        }
        if (need_immediate_flush) {
            cv_.notify_one();
        } else {
            cv_.notify_one();
        }
    }

    void flush() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!running_) return;

        // Signal flusher and wait until incoming is drained
        flush_requested_ = true;
        cv_.notify_one();
        cv_flush_done_.wait(lock, [this] {
            return incoming_.empty() && !flush_requested_;
        });

        // Also flush sinks
        for (auto& s : sinks_) {
            s->flush();
        }
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) return;
            running_ = false;
            cv_.notify_all();
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        // Final flush of sinks
        for (auto& s : sinks_) {
            s->flush();
        }
    }

    void update_sinks(std::vector<SinkPtr> sinks) {
        std::lock_guard<std::mutex> lock(mutex_);
        sinks_ = std::move(sinks);
    }

private:
    void worker_loop() {
        std::vector<Record> local_batch;
        local_batch.reserve(256);

        while (true) {
            std::vector<SinkPtr> local_sinks;
            bool was_flush_requested = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(flush_interval_ms_), [this] {
                    return !running_ || !incoming_.empty() || flush_requested_;
                });

                if (!running_ && incoming_.empty()) {
                    break;
                }

                local_batch.swap(incoming_);
                local_sinks = sinks_;
                was_flush_requested = flush_requested_;
                flush_requested_ = false;
            }

            bool has_error = false;
            if (!local_batch.empty()) {
                for (const auto& rec : local_batch) {
                    if (rec.level >= Level::Error) {
                        has_error = true;
                    }
                    for (const auto& sink : local_sinks) {
                        sink->log(rec);
                    }
                }
                local_batch.clear();
            }

            if (has_error || was_flush_requested) {
                for (const auto& sink : local_sinks) {
                    sink->flush();
                }
            }

            if (was_flush_requested) {
                std::lock_guard<std::mutex> lock(mutex_);
                cv_flush_done_.notify_all();
            }
        }
    }

    std::vector<SinkPtr> sinks_;
    size_t flush_interval_ms_;
    size_t max_queue_size_;
    std::atomic<bool> running_{false};
    bool flush_requested_{false};

    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable cv_flush_done_;
    std::vector<Record> incoming_;
    std::thread worker_;
};

} // namespace aegon::log
