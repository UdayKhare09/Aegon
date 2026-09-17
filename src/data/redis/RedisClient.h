#pragma once

#include "RedisConnectionPool.h"
#include "RedisSentinel.h"
#include "RedisCluster.h"
#include "RedisPipeline.h"
#include "RedisTransaction.h"
#include "RedisSubscriber.h"
#include "RedisStreamTypes.h"
#include "core/IoUring.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <memory>
#include <chrono>

namespace aegon::data::redis {

class RedisLock;

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
    core::Task<std::vector<std::string>> zrange(std::string_view key, int64_t start, int64_t stop);
    core::Task<std::vector<std::pair<std::string, double>>> zrange_with_scores(std::string_view key, int64_t start, int64_t stop);
    core::Task<std::vector<std::string>> zrevrange(std::string_view key, int64_t start, int64_t stop);
    core::Task<std::vector<std::pair<std::string, double>>> zrevrange_with_scores(std::string_view key, int64_t start, int64_t stop);
    core::Task<std::vector<std::string>> zrangebyscore(std::string_view key, std::string_view min, std::string_view max, int64_t offset = 0, int64_t count = -1);
    core::Task<std::vector<std::pair<std::string, double>>> zrangebyscore_with_scores(std::string_view key, std::string_view min, std::string_view max, int64_t offset = 0, int64_t count = -1);
    core::Task<int64_t> zcard(std::string_view key);
    core::Task<int64_t> zcount(std::string_view key, std::string_view min, std::string_view max);
    core::Task<std::optional<double>> zscore(std::string_view key, std::string_view member);
    core::Task<std::optional<int64_t>> zrank(std::string_view key, std::string_view member);
    core::Task<std::optional<int64_t>> zrevrank(std::string_view key, std::string_view member);

    // --- Streams API ---
    core::Task<std::string> xadd(std::string_view key, std::string_view id, const std::vector<std::pair<std::string, std::string>>& fields, std::optional<size_t> maxlen = std::nullopt);
    core::Task<std::vector<StreamReadResult>> xread(const std::vector<std::string>& streams, const std::vector<std::string>& ids, std::optional<size_t> count = std::nullopt, std::optional<std::chrono::milliseconds> block_ms = std::nullopt);
    core::Task<std::vector<StreamMessage>> xrange(std::string_view key, std::string_view start, std::string_view end, std::optional<size_t> count = std::nullopt);
    core::Task<std::vector<StreamMessage>> xrevrange(std::string_view key, std::string_view end, std::string_view start, std::optional<size_t> count = std::nullopt);
    core::Task<int64_t> xlen(std::string_view key);
    core::Task<bool> xgroup_create(std::string_view key, std::string_view group, std::string_view id = "$", bool mkstream = false);
    core::Task<std::vector<StreamReadResult>> xreadgroup(std::string_view group, std::string_view consumer, const std::vector<std::string>& streams, const std::vector<std::string>& ids, std::optional<size_t> count = std::nullopt, std::optional<std::chrono::milliseconds> block_ms = std::nullopt, bool noack = false);
    core::Task<int64_t> xack(std::string_view key, std::string_view group, const std::vector<std::string>& ids);
    core::Task<int64_t> xdel(std::string_view key, const std::vector<std::string>& ids);

    // --- Lua Scripting API ---
    core::Task<RespValue> eval(std::string_view script, const std::vector<std::string_view>& keys = {}, const std::vector<std::string_view>& args = {});
    core::Task<RespValue> evalsha(std::string_view sha1, const std::vector<std::string_view>& keys = {}, const std::vector<std::string_view>& args = {});
    core::Task<std::string> script_load(std::string_view script);
    core::Task<RespValue> eval_script(std::string_view script, const std::vector<std::string_view>& keys = {}, const std::vector<std::string_view>& args = {});

    // --- Distributed Locking API ---
    core::Task<std::optional<RedisLock>> lock(std::string_view key, std::chrono::milliseconds ttl, std::chrono::milliseconds retry_delay = std::chrono::milliseconds(50), size_t max_retries = 0);

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
