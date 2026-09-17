#include "RedisClient.h"
#include "RedisLock.h"
#include "data/uuid/UUIDGenerator.h"
#include <openssl/sha.h>
#include <thread>
#include <iomanip>

namespace aegon::data::redis {

static std::string compute_sha1(std::string_view input) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(input.data()), input.size(), hash);
    static const char hex_chars[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(SHA_DIGEST_LENGTH * 2);
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        hex.push_back(hex_chars[(hash[i] >> 4) & 0x0F]);
        hex.push_back(hex_chars[hash[i] & 0x0F]);
    }
    return hex;
}

static std::vector<StreamMessage> parse_stream_messages(const RespValue& resp) {
    std::vector<StreamMessage> messages;
    if (!resp.is_array()) return messages;
    for (const auto& msg_item : resp.as_array()) {
        if (!msg_item.is_array()) continue;
        const auto& item_arr = msg_item.as_array();
        if (item_arr.size() < 2) continue;
        StreamMessage msg;
        msg.id = std::string(item_arr[0].as_string());
        if (item_arr[1].is_array()) {
            const auto& fields_arr = item_arr[1].as_array();
            for (size_t i = 0; i + 1 < fields_arr.size(); i += 2) {
                msg.fields.emplace_back(std::string(fields_arr[i].as_string()), std::string(fields_arr[i + 1].as_string()));
            }
        }
        messages.push_back(std::move(msg));
    }
    return messages;
}

static std::vector<StreamReadResult> parse_stream_read_results(const RespValue& resp) {
    std::vector<StreamReadResult> results;
    if (!resp.is_array()) return results;
    for (const auto& stream_item : resp.as_array()) {
        if (!stream_item.is_array()) continue;
        const auto& stream_arr = stream_item.as_array();
        if (stream_arr.size() < 2) continue;
        StreamReadResult res;
        res.stream = std::string(stream_arr[0].as_string());
        res.messages = parse_stream_messages(stream_arr[1]);
        results.push_back(std::move(res));
    }
    return results;
}


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

core::Task<std::vector<std::string>> RedisClient::zrange(std::string_view key, int64_t start, int64_t stop) {
    std::string s_start = std::to_string(start);
    std::string s_stop = std::to_string(stop);
    auto resp = co_await execute(key, {"ZRANGE", key, s_start, s_stop});
    std::vector<std::string> res;
    if (resp.is_array()) {
        for (const auto& el : resp.as_array()) {
            if (el.is_string()) res.emplace_back(el.as_string());
        }
    }
    co_return res;
}

core::Task<std::vector<std::pair<std::string, double>>> RedisClient::zrange_with_scores(std::string_view key, int64_t start, int64_t stop) {
    std::string s_start = std::to_string(start);
    std::string s_stop = std::to_string(stop);
    auto resp = co_await execute(key, {"ZRANGE", key, s_start, s_stop, "WITHSCORES"});
    std::vector<std::pair<std::string, double>> res;
    if (resp.is_array()) {
        const auto& arr = resp.as_array();
        for (size_t i = 0; i + 1 < arr.size(); i += 2) {
            std::string member(arr[i].as_string());
            double score = 0.0;
            try { score = std::stod(std::string(arr[i + 1].as_string())); } catch (...) {}
            res.emplace_back(std::move(member), score);
        }
    }
    co_return res;
}

core::Task<std::vector<std::string>> RedisClient::zrevrange(std::string_view key, int64_t start, int64_t stop) {
    std::string s_start = std::to_string(start);
    std::string s_stop = std::to_string(stop);
    auto resp = co_await execute(key, {"ZREVRANGE", key, s_start, s_stop});
    std::vector<std::string> res;
    if (resp.is_array()) {
        for (const auto& el : resp.as_array()) {
            if (el.is_string()) res.emplace_back(el.as_string());
        }
    }
    co_return res;
}

core::Task<std::vector<std::pair<std::string, double>>> RedisClient::zrevrange_with_scores(std::string_view key, int64_t start, int64_t stop) {
    std::string s_start = std::to_string(start);
    std::string s_stop = std::to_string(stop);
    auto resp = co_await execute(key, {"ZREVRANGE", key, s_start, s_stop, "WITHSCORES"});
    std::vector<std::pair<std::string, double>> res;
    if (resp.is_array()) {
        const auto& arr = resp.as_array();
        for (size_t i = 0; i + 1 < arr.size(); i += 2) {
            std::string member(arr[i].as_string());
            double score = 0.0;
            try { score = std::stod(std::string(arr[i + 1].as_string())); } catch (...) {}
            res.emplace_back(std::move(member), score);
        }
    }
    co_return res;
}

core::Task<std::vector<std::string>> RedisClient::zrangebyscore(std::string_view key, std::string_view min, std::string_view max, int64_t offset, int64_t count) {
    std::vector<std::string_view> args = {"ZRANGEBYSCORE", key, min, max};
    std::string s_offset, s_count;
    if (count >= 0) {
        s_offset = std::to_string(offset);
        s_count = std::to_string(count);
        args.push_back("LIMIT");
        args.push_back(s_offset);
        args.push_back(s_count);
    }
    auto resp = co_await execute(key, args);
    std::vector<std::string> res;
    if (resp.is_array()) {
        for (const auto& el : resp.as_array()) {
            if (el.is_string()) res.emplace_back(el.as_string());
        }
    }
    co_return res;
}

core::Task<std::vector<std::pair<std::string, double>>> RedisClient::zrangebyscore_with_scores(std::string_view key, std::string_view min, std::string_view max, int64_t offset, int64_t count) {
    std::vector<std::string_view> args = {"ZRANGEBYSCORE", key, min, max, "WITHSCORES"};
    std::string s_offset, s_count;
    if (count >= 0) {
        s_offset = std::to_string(offset);
        s_count = std::to_string(count);
        args.push_back("LIMIT");
        args.push_back(s_offset);
        args.push_back(s_count);
    }
    auto resp = co_await execute(key, args);
    std::vector<std::pair<std::string, double>> res;
    if (resp.is_array()) {
        const auto& arr = resp.as_array();
        for (size_t i = 0; i + 1 < arr.size(); i += 2) {
            std::string member(arr[i].as_string());
            double score = 0.0;
            try { score = std::stod(std::string(arr[i + 1].as_string())); } catch (...) {}
            res.emplace_back(std::move(member), score);
        }
    }
    co_return res;
}

core::Task<int64_t> RedisClient::zcard(std::string_view key) {
    auto resp = co_await execute(key, {"ZCARD", key});
    if (resp.is_integer()) co_return resp.as_integer();
    co_return 0;
}

core::Task<int64_t> RedisClient::zcount(std::string_view key, std::string_view min, std::string_view max) {
    auto resp = co_await execute(key, {"ZCOUNT", key, min, max});
    if (resp.is_integer()) co_return resp.as_integer();
    co_return 0;
}

core::Task<std::optional<double>> RedisClient::zscore(std::string_view key, std::string_view member) {
    auto resp = co_await execute(key, {"ZSCORE", key, member});
    if (resp.is_string()) {
        try {
            co_return std::stod(std::string(resp.as_string()));
        } catch (...) {}
    }
    co_return std::nullopt;
}

core::Task<std::optional<int64_t>> RedisClient::zrank(std::string_view key, std::string_view member) {
    auto resp = co_await execute(key, {"ZRANK", key, member});
    if (resp.is_integer()) co_return resp.as_integer();
    co_return std::nullopt;
}

core::Task<std::optional<int64_t>> RedisClient::zrevrank(std::string_view key, std::string_view member) {
    auto resp = co_await execute(key, {"ZREVRANK", key, member});
    if (resp.is_integer()) co_return resp.as_integer();
    co_return std::nullopt;
}

core::Task<std::string> RedisClient::xadd(std::string_view key, std::string_view id, const std::vector<std::pair<std::string, std::string>>& fields, std::optional<size_t> maxlen) {
    std::vector<std::string_view> args = {"XADD", key};
    std::string s_maxlen;
    if (maxlen) {
        s_maxlen = std::to_string(*maxlen);
        args.push_back("MAXLEN");
        args.push_back("~");
        args.push_back(s_maxlen);
    }
    args.push_back(id);
    for (const auto& [f, v] : fields) {
        args.push_back(f);
        args.push_back(v);
    }
    auto resp = co_await execute(key, args);
    if (resp.is_string()) {
        co_return std::string(resp.as_string());
    }
    co_return "";
}

core::Task<std::vector<StreamReadResult>> RedisClient::xread(const std::vector<std::string>& streams, const std::vector<std::string>& ids, std::optional<size_t> count, std::optional<std::chrono::milliseconds> block_ms) {
    std::vector<std::string_view> args = {"XREAD"};
    std::string s_count, s_block;
    if (count) {
        s_count = std::to_string(*count);
        args.push_back("COUNT");
        args.push_back(s_count);
    }
    if (block_ms) {
        s_block = std::to_string(block_ms->count());
        args.push_back("BLOCK");
        args.push_back(s_block);
    }
    args.push_back("STREAMS");
    for (const auto& s : streams) args.push_back(s);
    for (const auto& id : ids) args.push_back(id);

    std::string_view route_key = streams.empty() ? "" : streams[0];
    auto resp = co_await execute(route_key, args);
    co_return parse_stream_read_results(resp);
}

core::Task<std::vector<StreamMessage>> RedisClient::xrange(std::string_view key, std::string_view start, std::string_view end, std::optional<size_t> count) {
    std::vector<std::string_view> args = {"XRANGE", key, start, end};
    std::string s_count;
    if (count) {
        s_count = std::to_string(*count);
        args.push_back("COUNT");
        args.push_back(s_count);
    }
    auto resp = co_await execute(key, args);
    co_return parse_stream_messages(resp);
}

core::Task<std::vector<StreamMessage>> RedisClient::xrevrange(std::string_view key, std::string_view end, std::string_view start, std::optional<size_t> count) {
    std::vector<std::string_view> args = {"XREVRANGE", key, end, start};
    std::string s_count;
    if (count) {
        s_count = std::to_string(*count);
        args.push_back("COUNT");
        args.push_back(s_count);
    }
    auto resp = co_await execute(key, args);
    co_return parse_stream_messages(resp);
}

core::Task<int64_t> RedisClient::xlen(std::string_view key) {
    auto resp = co_await execute(key, {"XLEN", key});
    if (resp.is_integer()) co_return resp.as_integer();
    co_return 0;
}

core::Task<bool> RedisClient::xgroup_create(std::string_view key, std::string_view group, std::string_view id, bool mkstream) {
    std::vector<std::string_view> args = {"XGROUP", "CREATE", key, group, id};
    if (mkstream) args.push_back("MKSTREAM");
    auto resp = co_await execute(key, args);
    co_return resp.is_string() && (resp.as_string() == "OK" || resp.as_string() == "+OK");
}

core::Task<std::vector<StreamReadResult>> RedisClient::xreadgroup(std::string_view group, std::string_view consumer, const std::vector<std::string>& streams, const std::vector<std::string>& ids, std::optional<size_t> count, std::optional<std::chrono::milliseconds> block_ms, bool noack) {
    std::vector<std::string_view> args = {"XREADGROUP", "GROUP", group, consumer};
    std::string s_count, s_block;
    if (count) {
        s_count = std::to_string(*count);
        args.push_back("COUNT");
        args.push_back(s_count);
    }
    if (block_ms) {
        s_block = std::to_string(block_ms->count());
        args.push_back("BLOCK");
        args.push_back(s_block);
    }
    if (noack) args.push_back("NOACK");
    args.push_back("STREAMS");
    for (const auto& s : streams) args.push_back(s);
    for (const auto& id : ids) args.push_back(id);

    std::string_view route_key = streams.empty() ? "" : streams[0];
    auto resp = co_await execute(route_key, args);
    co_return parse_stream_read_results(resp);
}

core::Task<int64_t> RedisClient::xack(std::string_view key, std::string_view group, const std::vector<std::string>& ids) {
    std::vector<std::string_view> args = {"XACK", key, group};
    for (const auto& id : ids) args.push_back(id);
    auto resp = co_await execute(key, args);
    if (resp.is_integer()) co_return resp.as_integer();
    co_return 0;
}

core::Task<int64_t> RedisClient::xdel(std::string_view key, const std::vector<std::string>& ids) {
    std::vector<std::string_view> args = {"XDEL", key};
    for (const auto& id : ids) args.push_back(id);
    auto resp = co_await execute(key, args);
    if (resp.is_integer()) co_return resp.as_integer();
    co_return 0;
}

core::Task<RespValue> RedisClient::eval(std::string_view script, const std::vector<std::string_view>& keys, const std::vector<std::string_view>& args) {
    std::string s_numkeys = std::to_string(keys.size());
    std::vector<std::string_view> cmd = {"EVAL", script, s_numkeys};
    cmd.reserve(3 + keys.size() + args.size());
    for (auto k : keys) cmd.push_back(k);
    for (auto a : args) cmd.push_back(a);
    if (!keys.empty()) {
        co_return co_await execute(keys[0], cmd);
    }
    co_return co_await execute(cmd);
}

core::Task<RespValue> RedisClient::evalsha(std::string_view sha1, const std::vector<std::string_view>& keys, const std::vector<std::string_view>& args) {
    std::string s_numkeys = std::to_string(keys.size());
    std::vector<std::string_view> cmd = {"EVALSHA", sha1, s_numkeys};
    cmd.reserve(3 + keys.size() + args.size());
    for (auto k : keys) cmd.push_back(k);
    for (auto a : args) cmd.push_back(a);
    if (!keys.empty()) {
        co_return co_await execute(keys[0], cmd);
    }
    co_return co_await execute(cmd);
}

core::Task<std::string> RedisClient::script_load(std::string_view script) {
    auto resp = co_await execute({"SCRIPT", "LOAD", script});
    if (resp.is_string()) {
        co_return std::string(resp.as_string());
    }
    co_return compute_sha1(script);
}

core::Task<RespValue> RedisClient::eval_script(std::string_view script, const std::vector<std::string_view>& keys, const std::vector<std::string_view>& args) {
    std::string sha = compute_sha1(script);
    auto resp = co_await evalsha(sha, keys, args);
    if (resp.is_error() && resp.as_string().starts_with("NOSCRIPT")) {
        resp = co_await eval(script, keys, args);
    }
    co_return resp;
}

core::Task<std::optional<RedisLock>> RedisClient::lock(std::string_view key, std::chrono::milliseconds ttl, std::chrono::milliseconds retry_delay, size_t max_retries) {
    std::string token = UUIDGenerator::v4().to_string();
    std::string ttl_str = std::to_string(ttl.count());
    size_t attempts = 0;
    while (true) {
        auto resp = co_await execute(key, {"SET", key, token, "NX", "PX", ttl_str});
        if (resp.is_string() && (resp.as_string() == "OK" || resp.as_string() == "+OK")) {
            co_return RedisLock(*this, std::string(key), std::move(token), ttl);
        }
        attempts++;
        if (attempts > max_retries) {
            break;
        }
        if (retry_delay.count() > 0) {
            std::this_thread::sleep_for(retry_delay);
        }
    }
    co_return std::nullopt;
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
        return RedisPipeline(*cluster_router_);
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
