#pragma once

#include "Connection.h"
#include <vector>
#include <memory>
#include <functional>
#include <stdexcept>

namespace aegon::data::orm::sql {

class PerCoreConnectionPool;

class ConnectionGuard {
    PerCoreConnectionPool* pool_{nullptr};
    std::unique_ptr<Connection> conn_;

public:
    ConnectionGuard() = default;
    ConnectionGuard(PerCoreConnectionPool* pool, std::unique_ptr<Connection> conn)
        : pool_(pool), conn_(std::move(conn)) {}

    ~ConnectionGuard();

    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;

    ConnectionGuard(ConnectionGuard&& other) noexcept
        : pool_(other.pool_), conn_(std::move(other.conn_)) {
        other.pool_ = nullptr;
    }

    ConnectionGuard& operator=(ConnectionGuard&& other) noexcept {
        if (this != &other) {
            reset();
            pool_ = other.pool_;
            conn_ = std::move(other.conn_);
            other.pool_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] Connection& get() noexcept { return *conn_; }
    [[nodiscard]] const Connection& get() const noexcept { return *conn_; }

    Connection& operator*() noexcept { return *conn_; }
    const Connection& operator*() const noexcept { return *conn_; }

    Connection* operator->() noexcept { return conn_.get(); }
    const Connection* operator->() const noexcept { return conn_.get(); }

    [[nodiscard]] bool valid() const noexcept { return conn_ != nullptr; }

    void reset();
};

#include <unordered_map>

class PerCoreConnectionPool {
    std::function<std::unique_ptr<Connection>()> factory_;
    size_t max_idle_{16};

    struct ThreadLocalPool {
        std::vector<std::unique_ptr<Connection>> available;
    };

    static inline thread_local std::unordered_map<const PerCoreConnectionPool*, ThreadLocalPool> t_pools;

public:
    explicit PerCoreConnectionPool(std::function<std::unique_ptr<Connection>()> factory, size_t max_idle = 16)
        : factory_(std::move(factory)), max_idle_(max_idle) {}

    ~PerCoreConnectionPool() {
        t_pools.erase(this);
    }

    [[nodiscard]] ConnectionGuard acquire() {
        auto& pool = t_pools[this];
        while (!pool.available.empty()) {
            auto conn = std::move(pool.available.back());
            pool.available.pop_back();
            if (conn && conn->is_valid()) {
                return ConnectionGuard(this, std::move(conn));
            }
        }

        if (!factory_) {
            throw std::runtime_error("PerCoreConnectionPool: no factory configured to create connection.");
        }

        auto new_conn = factory_();
        return ConnectionGuard(this, std::move(new_conn));
    }

    void release(std::unique_ptr<Connection> conn) {
        if (conn && conn->is_valid()) {
            auto& pool = t_pools[this];
            if (pool.available.size() < max_idle_) {
                pool.available.push_back(std::move(conn));
            }
        }
    }

    [[nodiscard]] size_t idle_count() const noexcept {
        auto it = t_pools.find(this);
        if (it != t_pools.end()) {
            return it->second.available.size();
        }
        return 0;
    }
};

inline ConnectionGuard::~ConnectionGuard() {
    reset();
}

inline void ConnectionGuard::reset() {
    if (pool_ && conn_) {
        pool_->release(std::move(conn_));
        pool_ = nullptr;
    }
    conn_.reset();
}

} // namespace aegon::data::orm::sql
