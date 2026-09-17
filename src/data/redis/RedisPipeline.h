#pragma once

#include "RedisConnectionPool.h"
#include "RedisCluster.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>
#include <unordered_map>

namespace aegon::data::redis {

class RedisPipeline {
public:
    explicit RedisPipeline(RedisConnectionPool& pool)
        : pool_(&pool), cluster_router_(nullptr) {}

    explicit RedisPipeline(RedisClusterRouter& router)
        : pool_(nullptr), cluster_router_(&router) {}

    RedisPipeline& set(std::string_view key, std::string_view val, std::optional<std::chrono::seconds> ttl = std::nullopt) {
        std::vector<std::string_view> cmd = {"SET", key, val};
        if (ttl.has_value()) {
            ttl_storage_.push_back(std::to_string(ttl->count()));
            cmd.push_back("EX");
            cmd.push_back(ttl_storage_.back());
        }
        commands_.push_back(copy_cmd(cmd));
        return *this;
    }

    RedisPipeline& get(std::string_view key) {
        commands_.push_back(copy_cmd({"GET", key}));
        return *this;
    }

    RedisPipeline& del(std::string_view key) {
        commands_.push_back(copy_cmd({"DEL", key}));
        return *this;
    }

    RedisPipeline& incr(std::string_view key) {
        commands_.push_back(copy_cmd({"INCR", key}));
        return *this;
    }

    RedisPipeline& decr(std::string_view key) {
        commands_.push_back(copy_cmd({"DECR", key}));
        return *this;
    }

    RedisPipeline& hget(std::string_view key, std::string_view field) {
        commands_.push_back(copy_cmd({"HGET", key, field}));
        return *this;
    }

    RedisPipeline& hset(std::string_view key, std::string_view field, std::string_view val) {
        commands_.push_back(copy_cmd({"HSET", key, field, val}));
        return *this;
    }

    RedisPipeline& command(std::vector<std::string_view> cmd) {
        commands_.push_back(copy_cmd(cmd));
        return *this;
    }

    [[nodiscard]] size_t size() const noexcept {
        return commands_.size();
    }

    core::Task<std::vector<RespValue>> execute() {
        if (commands_.empty()) {
            co_return std::vector<RespValue>{};
        }

        if (cluster_router_ != nullptr) {
            std::vector<RespValue> final_results(commands_.size());

            struct PoolBatch {
                std::shared_ptr<RedisConnectionPool> pool;
                std::vector<size_t> indices;
                std::vector<std::vector<std::string_view>> wire_cmds;
            };

            std::unordered_map<RedisConnectionPool*, PoolBatch> batches;
            for (size_t i = 0; i < commands_.size(); ++i) {
                const auto& cmd = commands_[i];
                std::string_view key = cmd.size() > 1 ? cmd[1] : "";
                auto pool = cluster_router_->pool_for_key(key);
                if (!pool) {
                    RespValue err;
                    err.type = RespType::Error;
                    err.data = std::string("No cluster pool for key");
                    final_results[i] = std::move(err);
                    continue;
                }

                auto& b = batches[pool.get()];
                if (!b.pool) b.pool = pool;
                b.indices.push_back(i);

                std::vector<std::string_view> views;
                views.reserve(cmd.size());
                for (const auto& s : cmd) views.push_back(s);
                b.wire_cmds.push_back(std::move(views));
            }

            for (auto& [_, batch] : batches) {
                auto guard = co_await batch.pool->acquire();
                if (!guard.conn) {
                    for (size_t idx : batch.indices) {
                        RespValue err;
                        err.type = RespType::Error;
                        err.data = std::string("Failed to acquire connection");
                        final_results[idx] = std::move(err);
                    }
                    continue;
                }
                auto results = co_await guard->execute_pipeline(batch.wire_cmds);
                for (size_t j = 0; j < results.size() && j < batch.indices.size(); ++j) {
                    final_results[batch.indices[j]] = std::move(results[j]);
                }
            }

            co_return final_results;
        }

        if (!pool_) {
            co_return std::vector<RespValue>{};
        }

        auto guard = co_await pool_->acquire();
        if (!guard.conn) {
            co_return std::vector<RespValue>{};
        }

        std::vector<std::vector<std::string_view>> wire_batch;
        wire_batch.reserve(commands_.size());
        for (const auto& cmd : commands_) {
            std::vector<std::string_view> views;
            views.reserve(cmd.size());
            for (const auto& s : cmd) {
                views.push_back(s);
            }
            wire_batch.push_back(std::move(views));
        }

        co_return co_await guard->execute_pipeline(wire_batch);
    }

private:
    std::vector<std::string> copy_cmd(const std::vector<std::string_view>& cmd) {
        std::vector<std::string> res;
        res.reserve(cmd.size());
        for (auto sv : cmd) res.emplace_back(sv);
        return res;
    }

    RedisConnectionPool* pool_{nullptr};
    RedisClusterRouter* cluster_router_{nullptr};
    std::vector<std::vector<std::string>> commands_;
    std::vector<std::string> ttl_storage_;
};

} // namespace aegon::data::redis
