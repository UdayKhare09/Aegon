#pragma once

#include "RedisConnection.h"
#include "core/IoUring.h"
#include "core/Task.h"
#include <vector>
#include <memory>
#include <mutex>

namespace aegon::data::redis {

class RedisConnectionPool {
public:
    RedisConnectionPool(core::IoUring& ring, RedisNodeConfig config, size_t pool_size = 8)
        : ring_(ring), config_(std::move(config)), max_size_(pool_size) {}

    ~RedisConnectionPool() {
        connections_.clear();
    }

    struct Guard {
        RedisConnectionPool& pool;
        std::unique_ptr<RedisConnection> conn;

        Guard(RedisConnectionPool& p, std::unique_ptr<RedisConnection> c) noexcept
            : pool(p), conn(std::move(c)) {}

        ~Guard() {
            if (conn) {
                pool.release(std::move(conn));
            }
        }

        Guard(Guard&& other) noexcept
            : pool(other.pool), conn(std::move(other.conn)) {}

        Guard& operator=(Guard&& other) noexcept {
            if (this != &other) {
                if (conn) pool.release(std::move(conn));
                conn = std::move(other.conn);
            }
            return *this;
        }

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        RedisConnection* operator->() noexcept { return conn.get(); }
        RedisConnection& operator*() noexcept { return *conn; }
    };

    core::Task<Guard> acquire() {
        std::unique_ptr<RedisConnection> conn;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!connections_.empty()) {
                conn = std::move(connections_.back());
                connections_.pop_back();
            }
        }

        if (!conn) {
            conn = std::make_unique<RedisConnection>(ring_, config_);
            co_await conn->connect();
        } else if (!conn->is_connected()) {
            co_await conn->connect();
        }

        co_return Guard{*this, std::move(conn)};
    }

    void release(std::unique_ptr<RedisConnection> conn) {
        if (!conn) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (connections_.size() < max_size_ && conn->is_connected()) {
            connections_.push_back(std::move(conn));
        }
    }

    core::Task<RespValue> execute(const std::vector<std::string_view>& args) {
        auto guard = co_await acquire();
        co_return co_await guard->execute(args);
    }

    core::Task<std::vector<RespValue>> execute_pipeline(const std::vector<std::vector<std::string_view>>& batch) {
        auto guard = co_await acquire();
        co_return co_await guard->execute_pipeline(batch);
    }

    [[nodiscard]] const RedisNodeConfig& config() const noexcept { return config_; }

private:
    core::IoUring& ring_;
    RedisNodeConfig config_;
    size_t max_size_{8};
    std::mutex mutex_;
    std::vector<std::unique_ptr<RedisConnection>> connections_;
};

} // namespace aegon::data::redis
