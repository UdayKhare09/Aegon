#include "PerCoreConnectionPool.h"

namespace aegon::data::orm::sql {

ConnectionGuard::~ConnectionGuard() {
    reset();
}

void ConnectionGuard::reset() {
    if (pool_ && conn_) {
        pool_->release(std::move(conn_));
        pool_ = nullptr;
    }
    conn_.reset();
    raw_conn_ = nullptr;
}

PerCoreConnectionPool::PerCoreConnectionPool(std::function<std::unique_ptr<Connection>()> factory, size_t capacity, bool pipelined)
    : factory_(std::move(factory)), capacity_(capacity), pipelined_(pipelined) {}

PerCoreConnectionPool::~PerCoreConnectionPool() {
    t_pools.erase(this);
}

ConnectionGuard PerCoreConnectionPool::acquire() {
    auto& pool = t_pools[this];

    if (pipelined_ && core::EventLoop::current() != nullptr) {
        if (pool.all_connections.empty()) {
            pool.all_connections.reserve(capacity_);
            for (size_t i = 0; i < capacity_; ++i) {
                if (factory_) {
                    auto c = factory_();
                    if (c && c->is_valid()) {
                        pool.all_connections.push_back(std::move(c));
                        ++pool.total_spawned;
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
        if (pool.total_spawned > 0) {
            --pool.total_spawned;
        }
    }

    if (pool.total_spawned < capacity_) {
        if (!factory_) {
            throw std::runtime_error("PerCoreConnectionPool: no factory configured to create connection.");
        }
        auto new_conn = factory_();
        if (new_conn && new_conn->is_valid()) {
            ++pool.total_spawned;
            return ConnectionGuard(this, std::move(new_conn));
        }
        throw std::runtime_error("PerCoreConnectionPool: factory produced invalid connection.");
    }

    throw std::runtime_error("PerCoreConnectionPool: all connections busy (capacity=" + std::to_string(capacity_) + ")");
}

void PerCoreConnectionPool::release(std::unique_ptr<Connection> conn) {
    auto& pool = t_pools[this];
    if (conn && conn->is_valid()) {
        if (pool.available.size() < capacity_) {
            pool.available.push_back(std::move(conn));
            return;
        }
    }
    if (conn && pool.total_spawned > 0) {
        --pool.total_spawned;
    }
}

size_t PerCoreConnectionPool::idle_count() const noexcept {
    auto it = t_pools.find(this);
    if (it != t_pools.end()) {
        if (pipelined_) return it->second.all_connections.size();
        return it->second.available.size();
    }
    return 0;
}

size_t PerCoreConnectionPool::total_spawned() const noexcept {
    auto it = t_pools.find(this);
    if (it != t_pools.end()) {
        return it->second.total_spawned;
    }
    return 0;
}

} // namespace aegon::data::orm::sql
