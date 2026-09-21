#include "MemStore.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <stdexcept>
#include <cstring>

namespace aegon::data::memory {

// ─────────────────────────────────────────────────────────────────────────────
// MemStore — construction / lifecycle
// ─────────────────────────────────────────────────────────────────────────────

MemStore::MemStore(MemStoreConfig cfg)
    : cfg_(cfg) {
    store_.reserve(std::min(cfg_.max_entries, size_t{65536}));
    // Start the background thread immediately — no separate start() call needed.
    thread_ = std::thread(&MemStore::thread_loop, this);
}

MemStore::~MemStore() {
    stop();
}

void MemStore::start() {
    // No-op: the thread starts automatically in the constructor.
    // Kept for API symmetry with stop().
}

void MemStore::stop() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard lk(queue_mtx_);
        stop_flag_ = true;
    }
    queue_cv_.notify_one();
    thread_.join();
}

void MemStore::submit(MemRequest* req) {
    {
        std::lock_guard lk(queue_mtx_);
        queue_.push_back(req);
    }
    queue_cv_.notify_one();
}

size_t MemStore::size() const noexcept {
    // store_ is only safe to read from MemStore thread, but size() is often
    // called for monitoring. Use a reasonable approximation via atomic.
    // For an exact count callers should call on the MemStore thread.
    return store_.size(); // benign data-race for stats only
}

// ─────────────────────────────────────────────────────────────────────────────
// Thread loop
// ─────────────────────────────────────────────────────────────────────────────

void MemStore::thread_loop() {
    std::vector<MemRequest*> batch;
    batch.reserve(256);

    while (true) {
        // Wait for work or stop signal
        {
            std::unique_lock lk(queue_mtx_);
            queue_cv_.wait_for(lk, cfg_.sweep_interval, [this] {
                return stop_flag_ || !queue_.empty();
            });

            if (stop_flag_ && queue_.empty()) break;

            std::swap(batch, queue_);
        }

        // Process all batched requests
        for (MemRequest* req : batch) {
            process(req);
        }
        batch.clear();

        // Periodic TTL sweep
        auto now = std::chrono::steady_clock::now();
        if (now - last_sweep_ >= cfg_.sweep_interval) {
            sweep_expired();
            last_sweep_ = now;
        }
    }

    // Drain any remaining requests on shutdown
    std::vector<MemRequest*> remaining;
    {
        std::lock_guard lk(queue_mtx_);
        std::swap(remaining, queue_);
    }
    for (MemRequest* req : remaining) {
        process(req);
    }
}

void MemStore::process(MemRequest* req) noexcept {
    switch (req->op) {
    case MemOp::Get:
        req->result_str = do_get(req->key);
        break;

    case MemOp::MGet:
        req->result_mstr.clear();
        req->result_mstr.reserve(req->keys.size());
        for (const auto& k : req->keys) {
            req->result_mstr.push_back(do_get(k));
        }
        break;

    case MemOp::Set:
        req->result_bool = do_set(req->key, std::move(req->value), req->ttl);
        break;

    case MemOp::Del:
        req->result_bool = do_del(req->key);
        break;

    case MemOp::DelMany:
        req->result_i64 = 0;
        for (const auto& k : req->keys) {
            if (do_del(k)) ++req->result_i64;
        }
        break;

    case MemOp::Incr:
        req->result_i64 = do_incr(req->key);
        break;
    }

    // Signal the waiting coroutine
    if (req->notify_fd >= 0) {
        uint64_t val = 1;
        (void)::write(req->notify_fd, &val, sizeof(val));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// KV primitives
// ─────────────────────────────────────────────────────────────────────────────

std::optional<std::string> MemStore::do_get(const std::string& key) noexcept {
    auto it = store_.find(key);
    if (it == store_.end()) {
        misses_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }
    if (it->second.entry.is_expired()) {
        lru_list_.erase(it->second.lru_it);
        store_.erase(it);
        misses_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }
    touch(it);
    hits_.fetch_add(1, std::memory_order_relaxed);
    return it->second.entry.value;
}

bool MemStore::do_set(const std::string& key, std::string value,
                      std::optional<std::chrono::seconds> ttl) {
    auto it = store_.find(key);
    if (it != store_.end()) {
        // Update existing: move to front of LRU
        lru_list_.erase(it->second.lru_it);
        lru_list_.push_front(key);
        it->second.lru_it = lru_list_.begin();
        it->second.entry.value = std::move(value);
        it->second.entry.expires_at = ttl
            ? std::optional{std::chrono::steady_clock::now() + *ttl}
            : std::nullopt;
    } else {
        // Evict if at capacity
        if (store_.size() >= cfg_.max_entries) {
            evict_lru();
        }
        lru_list_.push_front(key);
        Entry entry;
        entry.value = std::move(value);
        entry.expires_at = ttl
            ? std::optional{std::chrono::steady_clock::now() + *ttl}
            : std::nullopt;
        store_.emplace(key, MapEntry{std::move(entry), lru_list_.begin()});
    }
    return true;
}

bool MemStore::do_del(const std::string& key) noexcept {
    auto it = store_.find(key);
    if (it == store_.end()) return false;
    lru_list_.erase(it->second.lru_it);
    store_.erase(it);
    return true;
}

int64_t MemStore::do_incr(const std::string& key) {
    auto it = store_.find(key);
    int64_t val = 0;
    if (it != store_.end() && !it->second.entry.is_expired()) {
        try { val = std::stoll(it->second.entry.value); }
        catch (...) { val = 0; }
        ++val;
        it->second.entry.value = std::to_string(val);
        touch(it);
    } else {
        // Create or reset
        if (it != store_.end()) {
            lru_list_.erase(it->second.lru_it);
            store_.erase(it);
        }
        if (store_.size() >= cfg_.max_entries) evict_lru();
        val = 1;
        lru_list_.push_front(key);
        store_.emplace(key, MapEntry{Entry{std::to_string(val), std::nullopt}, lru_list_.begin()});
    }
    return val;
}

void MemStore::touch(std::unordered_map<std::string, MapEntry>::iterator it) noexcept {
    lru_list_.splice(lru_list_.begin(), lru_list_, it->second.lru_it);
}

// ─────────────────────────────────────────────────────────────────────────────
// Maintenance
// ─────────────────────────────────────────────────────────────────────────────

void MemStore::sweep_expired() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = store_.begin(); it != store_.end(); ) {
        if (it->second.entry.expires_at && now >= *it->second.entry.expires_at) {
            lru_list_.erase(it->second.lru_it);
            it = store_.erase(it);
        } else {
            ++it;
        }
    }
}

void MemStore::evict_lru() {
    if (lru_list_.empty()) return;
    const std::string& lru_key = lru_list_.back();
    auto it = store_.find(lru_key);
    if (it != store_.end()) {
        store_.erase(it);
    }
    lru_list_.pop_back();
}

} // namespace aegon::data::memory
