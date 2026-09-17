#pragma once

#include "RedisConnectionPool.h"
#include "RedisSentinel.h"
#include "RedisCluster.h"
#include "RedisPipeline.h"
#include "RedisTransaction.h"
#include "RedisSubscriber.h"
#include "core/IoUring.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <memory>
#include <chrono>

namespace aegon::data::redis {

enum class RedisMode {
    Standalone,
    Sentinel,
    Cluster
};

class RedisClient {
public:
    // Standalone constructor
    RedisClient(core::IoUring& ring, RedisNodeConfig config, size_t pool_size = 8);

    // Sentinel constructor
    RedisClient(core::IoUring& ring, SentinelConfig config, size_t pool_size = 8);

    // Cluster constructor
    RedisClient(core::IoUring& ring, ClusterConfig config);

    ~RedisClient() = default;

    // --- Core Strings API ---
    core::Task<std::optional<std::string>> get(std::string_view key);
    core::Task<bool> set(std::string_view key, std::string_view val, std::optional<std::chrono::seconds> ttl = std::nullopt);
    core::Task<std::vector<std::optional<std::string>>> mget(const std::vector<std::string>& keys);
    core::Task<bool> mset(const std::vector<std::pair<std::string, std::string>>& kvs);
    core::Task<bool> del(std::string_view key);
    core::Task<int64_t> del_many(const std::vector<std::string>& keys);
    core::Task<int64_t> incr(std::string_view key);
    core::Task<int64_t> decr(std::string_view key);

    // --- Hashes API ---
    core::Task<std::optional<std::string>> hget(std::string_view key, std::string_view field);
    core::Task<bool> hset(std::string_view key, std::string_view field, std::string_view val);
    core::Task<bool> hdel(std::string_view key, std::string_view field);
    core::Task<std::vector<std::pair<std::string, std::string>>> hgetall(std::string_view key);
    core::Task<bool> hexists(std::string_view key, std::string_view field);

    // --- Lists API ---
    core::Task<int64_t> lpush(std::string_view key, std::string_view val);
    core::Task<int64_t> rpush(std::string_view key, std::string_view val);
    core::Task<std::optional<std::string>> lpop(std::string_view key);
    core::Task<std::optional<std::string>> rpop(std::string_view key);

    // --- Sets API ---
    core::Task<bool> sadd(std::string_view key, std::string_view member);
    core::Task<bool> srem(std::string_view key, std::string_view member);
    core::Task<std::vector<std::string>> smembers(std::string_view key);
    core::Task<bool> sismember(std::string_view key, std::string_view member);

    // --- Sorted Sets API ---
    core::Task<bool> zadd(std::string_view key, std::string_view member, double score);
    core::Task<bool> zrem(std::string_view key, std::string_view member);

    // --- Key Expiry & Lifecycle API ---
    core::Task<bool> expire(std::string_view key, std::chrono::seconds seconds);
    core::Task<bool> pexpire(std::string_view key, std::chrono::milliseconds ms);
    core::Task<int64_t> ttl(std::string_view key);
    core::Task<int64_t> pttl(std::string_view key);
    core::Task<bool> persist(std::string_view key);
    core::Task<bool> exists(std::string_view key);
    core::Task<bool> select_db(uint32_t db);

    // --- Transactions & Pipelines ---
    core::Task<std::optional<RedisTransaction>> multi();
    RedisPipeline pipeline();

    // --- Pub/Sub Subscriber ---
    RedisSubscriber subscriber();

    // --- Pub/Sub ---
    core::Task<int64_t> publish(std::string_view channel, std::string_view message);

    // --- Raw Command Execution ---
    core::Task<RespValue> execute(const std::vector<std::string_view>& args);
    core::Task<RespValue> execute(std::string_view key, const std::vector<std::string_view>& args);

    [[nodiscard]] RedisMode mode() const noexcept { return mode_; }
    [[nodiscard]] const std::shared_ptr<RedisConnectionPool>& pool() const noexcept { return standalone_pool_; }

private:
    core::IoUring& ring_;
    RedisMode mode_{RedisMode::Standalone};
    RedisNodeConfig standalone_config_{};
    SentinelConfig sentinel_config_{};
    std::shared_ptr<RedisConnectionPool> standalone_pool_;
    std::unique_ptr<RedisSentinelResolver> sentinel_resolver_;
    std::unique_ptr<RedisClusterRouter> cluster_router_;
    size_t pool_size_{8};
};

} // namespace aegon::data::redis
