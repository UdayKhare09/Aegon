#include "RedisClient.h"

namespace aegon::data::redis {

RedisClient::RedisClient(core::IoUring& ring, RedisNodeConfig config, size_t pool_size)
    : ring_(ring), mode_(RedisMode::Standalone), standalone_config_(config), pool_size_(pool_size) {
    standalone_pool_ = std::make_shared<RedisConnectionPool>(ring_, std::move(config), pool_size_);
}

RedisClient::RedisClient(core::IoUring& ring, SentinelConfig config, size_t pool_size)
    : ring_(ring), mode_(RedisMode::Sentinel), sentinel_config_(config), pool_size_(pool_size) {
    sentinel_resolver_ = std::make_unique<RedisSentinelResolver>(ring_, std::move(config));
}

RedisClient::RedisClient(core::IoUring& ring, ClusterConfig config)
    : ring_(ring), mode_(RedisMode::Cluster) {
    cluster_router_ = std::make_unique<RedisClusterRouter>(ring_, std::move(config));
}

core::Task<RespValue> RedisClient::execute(const std::vector<std::string_view>& args) {
    if (args.empty()) {
        RespValue err;
        err.type = RespType::Error;
        err.data = std::string("Empty command");
        co_return err;
    }

    if (mode_ == RedisMode::Cluster) {
        // If key is present in arguments (typically args[1]), route by key
        std::string_view key = (args.size() > 1) ? args[1] : "";
        co_return co_await cluster_router_->execute(key, args);
    }

    if (mode_ == RedisMode::Sentinel) {
        if (!standalone_pool_) {
            auto mcfg = co_await sentinel_resolver_->discover_master();
            if (mcfg) {
                standalone_pool_ = std::make_shared<RedisConnectionPool>(ring_, std::move(*mcfg), pool_size_);
            } else {
                RespValue err;
                err.type = RespType::Error;
                err.data = std::string("Failed to discover master from Sentinel");
                co_return err;
            }
        }
    }

    auto resp = co_await standalone_pool_->execute(args);
    // If Sentinel master died, refresh
    if (mode_ == RedisMode::Sentinel && resp.is_error()) {
        auto mcfg = co_await sentinel_resolver_->discover_master();
        if (mcfg) {
            standalone_pool_ = std::make_shared<RedisConnectionPool>(ring_, std::move(*mcfg), pool_size_);
            co_return co_await standalone_pool_->execute(args);
        }
    }

    co_return resp;
}

core::Task<RespValue> RedisClient::execute(std::string_view key, const std::vector<std::string_view>& args) {
    if (mode_ == RedisMode::Cluster) {
        co_return co_await cluster_router_->execute(key, args);
    }
    co_return co_await execute(args);
}

core::Task<std::optional<std::string>> RedisClient::get(std::string_view key) {
    auto resp = co_await execute(key, {"GET", key});
    if (resp.is_string()) {
        co_return std::string(resp.as_string());
    }
    co_return std::nullopt;
}

core::Task<bool> RedisClient::set(std::string_view key, std::string_view val, std::optional<std::chrono::seconds> ttl) {
    std::vector<std::string_view> args;
    std::string ttl_str;
    if (ttl.has_value()) {
        ttl_str = std::to_string(ttl->count());
        args = {"SET", key, val, "EX", ttl_str};
    } else {
        args = {"SET", key, val};
    }

    auto resp = co_await execute(key, args);
    co_return !resp.is_error();
}

core::Task<std::vector<std::optional<std::string>>> RedisClient::mget(const std::vector<std::string>& keys) {
    std::vector<std::optional<std::string>> res;
    res.reserve(keys.size());

    if (keys.empty()) co_return res;

    if (mode_ == RedisMode::Cluster) {
        // In cluster, keys might belong to different slots. Execute sequentially or group by slot.
        for (const auto& k : keys) {
            res.push_back(co_await get(k));
        }
        co_return res;
    }

    std::vector<std::string_view> args;
    args.reserve(keys.size() + 1);
    args.push_back("MGET");
    for (const auto& k : keys) args.push_back(k);

    auto resp = co_await execute(args);
    if (resp.is_array()) {
        for (const auto& elem : resp.as_array()) {
            if (elem.is_string()) {
                res.push_back(std::string(elem.as_string()));
            } else {
                res.push_back(std::nullopt);
            }
        }
    } else {
        res.resize(keys.size(), std::nullopt);
    }

    co_return res;
}

core::Task<bool> RedisClient::mset(const std::vector<std::pair<std::string, std::string>>& kvs) {
    if (kvs.empty()) co_return true;

    if (mode_ == RedisMode::Cluster) {
        for (const auto& [k, v] : kvs) {
            bool ok = co_await set(k, v);
            if (!ok) co_return false;
        }
        co_return true;
    }

    std::vector<std::string_view> args;
    args.reserve(kvs.size() * 2 + 1);
    args.push_back("MSET");
    for (const auto& [k, v] : kvs) {
        args.push_back(k);
        args.push_back(v);
    }

    auto resp = co_await execute(args);
    co_return !resp.is_error();
}

core::Task<bool> RedisClient::del(std::string_view key) {
    auto resp = co_await execute(key, {"DEL", key});
    if (resp.is_integer()) {
        co_return resp.as_integer() > 0;
    }
    co_return false;
}

core::Task<int64_t> RedisClient::del_many(const std::vector<std::string>& keys) {
    if (keys.empty()) co_return 0;

    if (mode_ == RedisMode::Cluster) {
        int64_t count = 0;
        for (const auto& k : keys) {
            if (co_await del(k)) ++count;
        }
        co_return count;
    }

    std::vector<std::string_view> args;
    args.reserve(keys.size() + 1);
    args.push_back("DEL");
    for (const auto& k : keys) args.push_back(k);

    auto resp = co_await execute(args);
    if (resp.is_integer()) {
        co_return resp.as_integer();
    }
    co_return 0;
}

core::Task<int64_t> RedisClient::incr(std::string_view key) {
    auto resp = co_await execute(key, {"INCR", key});
    if (resp.is_integer()) {
        co_return resp.as_integer();
    }
    co_return 0;
}

core::Task<int64_t> RedisClient::decr(std::string_view key) {
    auto resp = co_await execute(key, {"DECR", key});
    if (resp.is_integer()) {
        co_return resp.as_integer();
    }
    co_return 0;
}

core::Task<std::optional<std::string>> RedisClient::hget(std::string_view key, std::string_view field) {
    auto resp = co_await execute(key, {"HGET", key, field});
    if (resp.is_string()) {
        co_return std::string(resp.as_string());
    }
    co_return std::nullopt;
}

core::Task<bool> RedisClient::hset(std::string_view key, std::string_view field, std::string_view val) {
    auto resp = co_await execute(key, {"HSET", key, field, val});
    co_return !resp.is_error();
}

core::Task<bool> RedisClient::hdel(std::string_view key, std::string_view field) {
    auto resp = co_await execute(key, {"HDEL", key, field});
    if (resp.is_integer()) {
        co_return resp.as_integer() > 0;
    }
    co_return false;
}

core::Task<std::vector<std::pair<std::string, std::string>>> RedisClient::hgetall(std::string_view key) {
    std::vector<std::pair<std::string, std::string>> res;
    auto resp = co_await execute(key, {"HGETALL", key});
    if (resp.is_array()) {
        const auto& arr = resp.as_array();
        for (size_t i = 0; i + 1 < arr.size(); i += 2) {
            res.emplace_back(std::string(arr[i].as_string()), std::string(arr[i + 1].as_string()));
        }
    }
    co_return res;
}

core::Task<bool> RedisClient::hexists(std::string_view key, std::string_view field) {
    auto resp = co_await execute(key, {"HEXISTS", key, field});
    if (resp.is_integer()) {
        co_return resp.as_integer() == 1;
    }
    co_return false;
}

core::Task<int64_t> RedisClient::lpush(std::string_view key, std::string_view val) {
    auto resp = co_await execute(key, {"LPUSH", key, val});
    if (resp.is_integer()) {
        co_return resp.as_integer();
    }
    co_return 0;
}

core::Task<int64_t> RedisClient::rpush(std::string_view key, std::string_view val) {
    auto resp = co_await execute(key, {"RPUSH", key, val});
    if (resp.is_integer()) {
        co_return resp.as_integer();
    }
    co_return 0;
}

core::Task<std::optional<std::string>> RedisClient::lpop(std::string_view key) {
    auto resp = co_await execute(key, {"LPOP", key});
    if (resp.is_string()) {
        co_return std::string(resp.as_string());
    }
    co_return std::nullopt;
}

core::Task<std::optional<std::string>> RedisClient::rpop(std::string_view key) {
    auto resp = co_await execute(key, {"RPOP", key});
    if (resp.is_string()) {
        co_return std::string(resp.as_string());
    }
    co_return std::nullopt;
}

core::Task<bool> RedisClient::sadd(std::string_view key, std::string_view member) {
    auto resp = co_await execute(key, {"SADD", key, member});
    if (resp.is_integer()) {
        co_return resp.as_integer() > 0;
    }
    co_return false;
}

core::Task<bool> RedisClient::srem(std::string_view key, std::string_view member) {
    auto resp = co_await execute(key, {"SREM", key, member});
    if (resp.is_integer()) {
        co_return resp.as_integer() > 0;
    }
    co_return false;
}

core::Task<std::vector<std::string>> RedisClient::smembers(std::string_view key) {
    std::vector<std::string> res;
    auto resp = co_await execute(key, {"SMEMBERS", key});
    if (resp.is_array()) {
        for (const auto& elem : resp.as_array()) {
            if (elem.is_string()) {
                res.emplace_back(elem.as_string());
            }
        }
    }
    co_return res;
}

core::Task<bool> RedisClient::sismember(std::string_view key, std::string_view member) {
    auto resp = co_await execute(key, {"SISMEMBER", key, member});
    if (resp.is_integer()) {
        co_return resp.as_integer() == 1;
    }
    co_return false;
}

core::Task<bool> RedisClient::zadd(std::string_view key, std::string_view member, double score) {
    std::string score_str = std::to_string(score);
    auto resp = co_await execute(key, {"ZADD", key, score_str, member});
    co_return !resp.is_error();
}

core::Task<bool> RedisClient::zrem(std::string_view key, std::string_view member) {
    auto resp = co_await execute(key, {"ZREM", key, member});
    if (resp.is_integer()) {
        co_return resp.as_integer() > 0;
    }
    co_return false;
}

core::Task<int64_t> RedisClient::publish(std::string_view channel, std::string_view message) {
    auto resp = co_await execute({"PUBLISH", channel, message});
    if (resp.is_integer()) {
        co_return resp.as_integer();
    }
    co_return 0;
}

core::Task<bool> RedisClient::expire(std::string_view key, std::chrono::seconds seconds) {
    std::string sec_str = std::to_string(seconds.count());
    auto resp = co_await execute(key, {"EXPIRE", key, sec_str});
    if (resp.is_integer()) {
        co_return resp.as_integer() == 1;
    }
    co_return false;
}

core::Task<bool> RedisClient::pexpire(std::string_view key, std::chrono::milliseconds ms) {
    std::string ms_str = std::to_string(ms.count());
    auto resp = co_await execute(key, {"PEXPIRE", key, ms_str});
    if (resp.is_integer()) {
        co_return resp.as_integer() == 1;
    }
    co_return false;
}

core::Task<int64_t> RedisClient::ttl(std::string_view key) {
    auto resp = co_await execute(key, {"TTL", key});
    if (resp.is_integer()) {
        co_return resp.as_integer();
    }
    co_return -2;
}

core::Task<int64_t> RedisClient::pttl(std::string_view key) {
    auto resp = co_await execute(key, {"PTTL", key});
    if (resp.is_integer()) {
        co_return resp.as_integer();
    }
    co_return -2;
}

core::Task<bool> RedisClient::persist(std::string_view key) {
    auto resp = co_await execute(key, {"PERSIST", key});
    if (resp.is_integer()) {
        co_return resp.as_integer() == 1;
    }
    co_return false;
}

core::Task<bool> RedisClient::exists(std::string_view key) {
    auto resp = co_await execute(key, {"EXISTS", key});
    if (resp.is_integer()) {
        co_return resp.as_integer() == 1;
    }
    co_return false;
}

core::Task<bool> RedisClient::select_db(uint32_t db) {
    std::string db_str = std::to_string(db);
    auto resp = co_await execute({"SELECT", db_str});
    co_return !resp.is_error();
}

core::Task<std::optional<RedisTransaction>> RedisClient::multi() {
    if (mode_ == RedisMode::Cluster) {
        throw std::runtime_error("RedisClient::multi() is not supported across a distributed cluster; use standalone or sentinel");
    }
    if (mode_ == RedisMode::Sentinel && !standalone_pool_) {
        auto mcfg = co_await sentinel_resolver_->discover_master();
        if (mcfg) {
            standalone_pool_ = std::make_shared<RedisConnectionPool>(ring_, std::move(*mcfg), pool_size_);
        } else {
            co_return std::nullopt;
        }
    }
    co_return co_await RedisTransaction::begin(*standalone_pool_);
}

RedisPipeline RedisClient::pipeline() {
    if (mode_ == RedisMode::Cluster) {
        throw std::runtime_error("RedisClient::pipeline() requires standalone or sentinel pool");
    }
    return RedisPipeline(*standalone_pool_);
}

RedisSubscriber RedisClient::subscriber() {
    if (mode_ == RedisMode::Standalone) {
        return RedisSubscriber(ring_, standalone_config_);
    }
    // Sentinel mode config
    RedisNodeConfig cfg{
        .host = sentinel_config_.sentinels.empty() ? "127.0.0.1" : sentinel_config_.sentinels[0].first,
        .port = sentinel_config_.sentinels.empty() ? static_cast<uint16_t>(6379) : sentinel_config_.sentinels[0].second,
        .username = sentinel_config_.username,
        .password = sentinel_config_.password,
        .database = sentinel_config_.database
    };
    return RedisSubscriber(ring_, cfg);
}

} // namespace aegon::data::redis
