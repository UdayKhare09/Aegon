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
        size_t total_spawned{0};
        size_t rr_index{0};
    };

    static inline thread_local std::unordered_map<const PerCoreConnectionPool*, ThreadLocalPool> t_pools;

public:
    explicit PerCoreConnectionPool(std::function<std::unique_ptr<Connection>()> factory, size_t capacity = 16, bool pipelined = false);
    ~PerCoreConnectionPool();

    [[nodiscard]] ConnectionGuard acquire();
    void release(std::unique_ptr<Connection> conn);

    [[nodiscard]] size_t idle_count() const noexcept;
    [[nodiscard]] size_t total_spawned() const noexcept;
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
};

} // namespace aegon::data::orm::sql
