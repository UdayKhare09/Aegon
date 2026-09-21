#pragma once

#include "core/IoUring.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <list>
#include <memory>
#include <cstdint>

#include <sys/eventfd.h>
#include <unistd.h>
#include <liburing.h>

namespace aegon::data::memory {

// ─────────────────────────────────────────────────────────────────────────────
// MemStore Configuration
// ─────────────────────────────────────────────────────────────────────────────

struct MemStoreConfig {
    /// Maximum number of entries before LRU eviction kicks in.
    size_t max_entries{1'000'000};
    /// How often the TTL sweep runs (approximate: checked between queue drains).
    std::chrono::milliseconds sweep_interval{500};
};

// ─────────────────────────────────────────────────────────────────────────────
// Internal op types
// ─────────────────────────────────────────────────────────────────────────────

enum class MemOp : uint8_t {
    Get,
    MGet,
    Set,
    Del,
    DelMany,
    Incr,
};

/**
 * @brief A single request posted from an HTTP worker coroutine to the MemStore thread.
 *
 * Workers allocate this on the stack, call MemStore::submit(), then co_await the
 * EventFdReadAwaiter on notify_fd. The MemStore thread fills the result fields and
 * writes 1 to notify_fd, waking the coroutine via io_uring.
 */
struct MemRequest {
    MemOp op{MemOp::Get};

    // --- Input ---
    std::string  key;
    std::string  value;
    std::optional<std::chrono::seconds> ttl;
    std::vector<std::string> keys;   // MGet / DelMany

    // --- Output (written by MemStore thread before signalling notify_fd) ---
    std::optional<std::string>              result_str;
    std::vector<std::optional<std::string>> result_mstr;
    bool                                    result_bool{false};
    int64_t                                 result_i64{0};

    /// eventfd created by the worker. MemStore writes 1 here when done.
    int notify_fd{-1};
};

// ─────────────────────────────────────────────────────────────────────────────
// MemStore — the dedicated-thread in-process KV engine
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Async in-process key-value store backed by a single dedicated thread.
 *
 * The background thread starts automatically in the constructor — no separate
 * start() call is needed. Just construct, optionally register with
 * server.provide<MemStore>(), and use.
 *
 * Design:
 *   • The KV map (std::unordered_map + LRU list) is owned exclusively by the
 *     MemStore thread — no locking on the data path.
 *   • HTTP worker coroutines post MemRequest pointers into a mutex-protected
 *     queue and co_await an io_uring eventfd read.
 *   • The MemStore thread drains the queue, processes all requests, writes
 *     results into the MemRequest structs, and wakes each caller via eventfd_write.
 *   • TTL expiry sweep and LRU eviction run on the MemStore thread between drains.
 */
class MemStore {
public:
    explicit MemStore(MemStoreConfig cfg = {});
    ~MemStore();

    MemStore(const MemStore&)            = delete;
    MemStore& operator=(const MemStore&) = delete;
    MemStore(MemStore&&)                 = delete;
    MemStore& operator=(MemStore&&)      = delete;

    /// No-op — the thread starts automatically in the constructor.
    /// Kept for API symmetry with stop().
    void start();

    /// Gracefully stop: signals the thread, drains remaining requests, joins.
    /// Called automatically in the destructor.
    void stop();

    /// Enqueue a request. The caller must co_await the eventfd before reading results.
    void submit(MemRequest* req);

    // ── Stats ──────────────────────────────────────────────────────────────
    [[nodiscard]] size_t   size()   const noexcept;
    [[nodiscard]] uint64_t hits()   const noexcept { return hits_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t misses() const noexcept { return misses_.load(std::memory_order_relaxed); }
    void reset_stats() noexcept {
        hits_.store(0, std::memory_order_relaxed);
        misses_.store(0, std::memory_order_relaxed);
    }

private:
    // ── KV Entry ─────────────────────────────────────────────────────────
    struct Entry {
        std::string value;
        std::optional<std::chrono::steady_clock::time_point> expires_at;

        [[nodiscard]] bool is_expired() const noexcept {
            if (!expires_at) return false;
            return std::chrono::steady_clock::now() >= *expires_at;
        }
    };

    using LruList = std::list<std::string>;
    using LruIter = LruList::iterator;

    struct MapEntry {
        Entry   entry;
        LruIter lru_it;
    };

    // ── Thread ────────────────────────────────────────────────────────────
    void thread_loop();
    void drain_queue(std::vector<MemRequest*>& batch);
    void process(MemRequest* req) noexcept;
    void sweep_expired();
    void evict_lru();

    // ── KV primitives — only called from the MemStore thread ──────────────
    std::optional<std::string> do_get(const std::string& key) noexcept;
    bool   do_set(const std::string& key, std::string value, std::optional<std::chrono::seconds> ttl);
    bool   do_del(const std::string& key) noexcept;
    int64_t do_incr(const std::string& key);

    void touch(std::unordered_map<std::string, MapEntry>::iterator it) noexcept;

    // ── Members ───────────────────────────────────────────────────────────
    MemStoreConfig cfg_;
    std::thread    thread_;

    std::mutex              queue_mtx_;
    std::condition_variable queue_cv_;
    std::vector<MemRequest*> queue_;
    bool stop_flag_{false};

    // Exclusively owned by the MemStore thread — no locking needed:
    std::unordered_map<std::string, MapEntry> store_;
    LruList lru_list_;
    std::chrono::steady_clock::time_point last_sweep_{std::chrono::steady_clock::now()};

    mutable std::atomic<uint64_t> hits_{0};
    mutable std::atomic<uint64_t> misses_{0};
};

// ─────────────────────────────────────────────────────────────────────────────
// EventFdReadAwaiter — suspends coroutine until MemStore signals the eventfd
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Submits io_uring_prep_read on an eventfd and suspends the calling
 *        coroutine. Resumed by IoUring::process_completions() when the MemStore
 *        thread writes 1 to the fd.
 */
struct EventFdReadAwaiter : core::IoAwaiter {
    core::IoUring& ring;
    int            efd;
    uint64_t       buf{0};

    EventFdReadAwaiter(core::IoUring& r, int fd) noexcept
        : ring(r), efd(fd) {}

    void submit() noexcept override {
        struct io_uring_sqe* sqe = io_uring_get_sqe(ring.raw_ring());
        if (!sqe) [[unlikely]] return;
        io_uring_prep_read(sqe, efd, &buf, sizeof(buf), 0);
        io_uring_sqe_set_data(sqe, static_cast<core::IoAwaiter*>(this));
        io_uring_submit(ring.raw_ring());
    }

    // The result is in the MemRequest struct; nothing to return here.
    void await_resume() noexcept {}
};

} // namespace aegon::data::memory
