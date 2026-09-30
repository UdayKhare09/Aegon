#include "log/Logger.h"
#include "log/AsyncQueue.h"
#include <mutex>
#include <atomic>
#include <thread>

namespace aegon::log {

namespace {

std::atomic<Level> g_level{Level::Info};
std::atomic<bool> g_initialized{false};
std::mutex g_state_mutex;
std::vector<SinkPtr> g_sinks;
std::unique_ptr<AsyncQueue> g_async_queue;

uint64_t current_thread_id() noexcept {
    return static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

void ensure_initialized_locked() {
    if (!g_initialized.load(std::memory_order_relaxed)) {
        if (g_sinks.empty()) {
            g_sinks.push_back(std::make_shared<ConsoleSink>());
        }
        g_initialized.store(true, std::memory_order_release);
    }
}

} // namespace

void init(LoggerConfig config) {
    std::lock_guard<std::mutex> lock(g_state_mutex);

    g_level.store(config.level, std::memory_order_release);

    if (g_sinks.empty()) {
        g_sinks.push_back(std::make_shared<ConsoleSink>());
    }

    if (config.async) {
        if (!g_async_queue) {
            g_async_queue = std::make_unique<AsyncQueue>(
                g_sinks, config.flush_interval_ms, config.max_queue_size);
        } else {
            g_async_queue->update_sinks(g_sinks);
        }
    } else {
        if (g_async_queue) {
            g_async_queue->stop();
            g_async_queue.reset();
        }
    }

    g_initialized.store(true, std::memory_order_release);
}

void set_level(Level level) noexcept {
    g_level.store(level, std::memory_order_release);
}

Level get_level() noexcept {
    return g_level.load(std::memory_order_relaxed);
}

void add_sink(SinkPtr sink) {
    if (!sink) return;
    std::lock_guard<std::mutex> lock(g_state_mutex);
    ensure_initialized_locked();
    g_sinks.push_back(std::move(sink));
    if (g_async_queue) {
        g_async_queue->update_sinks(g_sinks);
    }
}

void set_sinks(std::vector<SinkPtr> sinks) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_sinks = std::move(sinks);
    g_initialized.store(true, std::memory_order_release);
    if (g_async_queue) {
        g_async_queue->update_sinks(g_sinks);
    }
}

void clear_sinks() {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_sinks.clear();
    if (g_async_queue) {
        g_async_queue->update_sinks(g_sinks);
    }
}

void flush() {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (g_async_queue) {
        g_async_queue->flush();
    } else {
        for (auto& s : g_sinks) {
            s->flush();
        }
    }
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (g_async_queue) {
        g_async_queue->stop();
        g_async_queue.reset();
    }
    for (auto& s : g_sinks) {
        s->flush();
    }
    g_initialized.store(false, std::memory_order_release);
}

void dispatch(Level level, std::source_location loc, std::string message) {
    Record record{
        .timestamp = std::chrono::system_clock::now(),
        .level = level,
        .message = std::move(message),
        .location = loc,
        .thread_id = current_thread_id()
    };

    // Fast check if async queue is active
    if (!g_initialized.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        ensure_initialized_locked();
    }

    std::vector<SinkPtr> sinks_snapshot;

    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (g_async_queue) {
            g_async_queue->push(std::move(record));
            return;
        }
        sinks_snapshot = g_sinks;
    }

    // Synchronous execution path
    bool is_error = record.level >= Level::Error;
    for (auto& sink : sinks_snapshot) {
        sink->log(record);
        if (is_error) {
            sink->flush();
        }
    }
}

} // namespace aegon::log
