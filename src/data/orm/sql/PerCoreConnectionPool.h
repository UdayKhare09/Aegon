#pragma once

#include "Connection.h"
#include "core/EventLoop.h"
#include <vector>
#include <memory>
#include <functional>
#include <stdexcept>

namespace aegon::data::orm::sql {

class PerCoreConnectionPool;

class ConnectionGuard {
    PerCoreConnectionPool* pool_{nullptr};
    std::unique_ptr<Connection> conn_;
    Connection* raw_conn_{nullptr};

public:
    ConnectionGuard() = default;
    ConnectionGuard(PerCoreConnectionPool* pool, std::unique_ptr<Connection> conn)
        : pool_(pool), conn_(std::move(conn)), raw_conn_(conn_.get()) {}

    explicit ConnectionGuard(Connection* raw)
        : pool_(nullptr), conn_(nullptr), raw_conn_(raw) {}

    ~ConnectionGuard();

    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;

    ConnectionGuard(ConnectionGuard&& other) noexcept
        : pool_(other.pool_), conn_(std::move(other.conn_)), raw_conn_(other.raw_conn_) {
        other.pool_ = nullptr;
        other.raw_conn_ = nullptr;
    }

    ConnectionGuard& operator=(ConnectionGuard&& other) noexcept {
        if (this != &other) {
            reset();
            pool_ = other.pool_;
            conn_ = std::move(other.conn_);
            raw_conn_ = other.raw_conn_;
            other.pool_ = nullptr;
            other.raw_conn_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] Connection& get() noexcept { return *raw_conn_; }
    [[nodiscard]] const Connection& get() const noexcept { return *raw_conn_; }

    Connection& operator*() noexcept { return *raw_conn_; }
    const Connection& operator*() const noexcept { return *raw_conn_; }

    Connection* operator->() noexcept { return raw_conn_; }
    const Connection* operator->() const noexcept { return raw_conn_; }

    [[nodiscard]] bool valid() const noexcept { return raw_conn_ != nullptr; }

    void reset();
};

#include <unordered_map>

class PerCoreConnectionPool {
    std::function<std::unique_ptr<Connection>()> factory_;
    size_t capacity_{16};
    bool pipelined_{false};

    struct ThreadLocalPool {
        std::vector<std::unique_ptr<Connection>> all_connections;
        std::vector<std::unique_ptr<Connection>> available;
        size_t rr_index{0};
    };

    static inline thread_local std::unordered_map<const PerCoreConnectionPool*, ThreadLocalPool> t_pools;

public:
    explicit PerCoreConnectionPool(std::function<std::unique_ptr<Connection>()> factory, size_t capacity = 16, bool pipelined = false)
        : factory_(std::move(factory)), capacity_(capacity), pipelined_(pipelined) {}

    ~PerCoreConnectionPool() {
        t_pools.erase(this);
    }

    [[nodiscard]] ConnectionGuard acquire() {
        auto& pool = t_pools[this];

        if (pipelined_ && core::EventLoop::current() != nullptr) {
            if (pool.all_connections.empty()) {
                pool.all_connections.reserve(capacity_);
                for (size_t i = 0; i < capacity_; ++i) {
                    if (factory_) {
                        auto c = factory_();
                        if (c && c->is_valid()) {
                            pool.all_connections.push_back(std::move(c));
                        }
                    }
                }
            }

            if (!pool.all_connections.empty()) {
                Connection* best = pool.all_connections[0].get();
                size_t min_flight = best->in_flight_count();
                for (size_t i = 1; i < pool.all_connections.size(); ++i) {
                    size_t cur = pool.all_connections[i]->in_flight_count();
                    if (cur < min_flight) {
                        min_flight = cur;
                        best = pool.all_connections[i].get();
                    }
                }
                return ConnectionGuard(best);
            }
        }

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
            if (pool.available.size() < capacity_) {
                pool.available.push_back(std::move(conn));
            }
        }
    }

    [[nodiscard]] size_t idle_count() const noexcept {
        auto it = t_pools.find(this);
        if (it != t_pools.end()) {
            if (pipelined_) return it->second.all_connections.size();
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
    raw_conn_ = nullptr;
}

} // namespace aegon::data::orm::sql
