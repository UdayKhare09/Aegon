#pragma once

#include "CacheBackend.h"
#include <mutex>
#include <unordered_map>
#include <chrono>

namespace aegon::data::cache {

class InMemoryCacheBackend : public CacheBackend {
public:
    struct Entry {
        std::string value;
        std::optional<std::chrono::steady_clock::time_point> expires_at;

        [[nodiscard]] bool is_expired() const noexcept {
            if (!expires_at) return false;
            return std::chrono::steady_clock::now() >= *expires_at;
        }
    };

    core::Task<std::optional<std::string>> get(std::string_view key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = store_.find(std::string(key));
        if (it != store_.end()) {
            if (it->second.is_expired()) {
                store_.erase(it);
                co_return std::nullopt;
            }
            co_return it->second.value;
        }
        co_return std::nullopt;
    }

    core::Task<std::vector<std::optional<std::string>>> mget(const std::vector<std::string>& keys) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::optional<std::string>> res;
        res.reserve(keys.size());
        auto now = std::chrono::steady_clock::now();
        for (const auto& k : keys) {
            auto it = store_.find(k);
            if (it != store_.end()) {
                if (it->second.expires_at && now >= *it->second.expires_at) {
                    store_.erase(it);
                    res.push_back(std::nullopt);
                } else {
                    res.push_back(it->second.value);
                }
            } else {
                res.push_back(std::nullopt);
            }
        }
        co_return res;
    }

    core::Task<bool> set(std::string_view key, std::string_view val, std::optional<std::chrono::seconds> ttl = std::nullopt) override {
        std::lock_guard<std::mutex> lock(mutex_);
        Entry entry;
        entry.value = std::string(val);
        if (ttl) {
            entry.expires_at = std::chrono::steady_clock::now() + *ttl;
        }
        store_[std::string(key)] = std::move(entry);
        co_return true;
    }

    core::Task<bool> del(std::string_view key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        bool removed = store_.erase(std::string(key)) > 0;
        co_return removed;
    }

    core::Task<int64_t> del_many(const std::vector<std::string>& keys) override {
        std::lock_guard<std::mutex> lock(mutex_);
        int64_t count = 0;
        for (const auto& k : keys) {
            if (store_.erase(k) > 0) {
                ++count;
            }
        }
        co_return count;
    }

    core::Task<int64_t> incr(std::string_view key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& entry = store_[std::string(key)];
        int64_t val = 0;
        if (!entry.value.empty()) {
            try {
                val = std::stoll(entry.value);
            } catch (...) {
                val = 0;
            }
        }
        ++val;
        entry.value = std::to_string(val);
        co_return val;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        store_.clear();
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> store_;
};

} // namespace aegon::data::cache
