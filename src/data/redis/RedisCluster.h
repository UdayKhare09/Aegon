#pragma once

#include "Crc16.h"
#include "RedisConnectionPool.h"
#include "core/IoUring.h"
#include "core/Task.h"
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <memory>
#include <sstream>

namespace aegon::data::redis {

struct ClusterConfig {
    std::vector<std::pair<std::string, uint16_t>> seed_nodes{{"127.0.0.1", 7000}};
    std::string password;
    size_t pool_size_per_node{8};
};

class RedisClusterRouter {
public:
    RedisClusterRouter(core::IoUring& ring, ClusterConfig config)
        : ring_(ring), config_(std::move(config)) {}

    core::Task<bool> initialize() {
        co_return co_await refresh_slots();
    }

    std::shared_ptr<RedisConnectionPool> get_or_create_pool(const std::string& host, uint16_t port) {
        std::string key = host + ":" + std::to_string(port);
        auto it = node_pools_.find(key);
        if (it != node_pools_.end()) {
            return it->second;
        }

        RedisNodeConfig cfg{
            .host = host,
            .port = port,
            .password = config_.password
        };
        auto pool = std::make_shared<RedisConnectionPool>(ring_, cfg, config_.pool_size_per_node);
        node_pools_[key] = pool;
        return pool;
    }

    core::Task<bool> refresh_slots() {
        for (const auto& [host, port] : config_.seed_nodes) {
            auto pool = get_or_create_pool(host, port);
            auto resp = co_await pool->execute({"CLUSTER", "SLOTS"});
            if (resp.is_array()) {
                const auto& slots_arr = resp.as_array();
                for (const auto& slot_range_val : slots_arr) {
                    if (!slot_range_val.is_array()) continue;
                    const auto& range_arr = slot_range_val.as_array();
                    if (range_arr.size() < 3) continue;

                    int64_t start_slot = range_arr[0].as_integer();
                    int64_t end_slot = range_arr[1].as_integer();

                    if (!range_arr[2].is_array() || range_arr[2].as_array().size() < 2) continue;
                    const auto& node_info = range_arr[2].as_array();
                    std::string node_ip = std::string(node_info[0].as_string());
                    uint16_t node_port = static_cast<uint16_t>(node_info[1].as_integer());

                    auto node_pool = get_or_create_pool(node_ip, node_port);
                    for (int64_t s = start_slot; s <= end_slot && s < 16384; ++s) {
                        slots_[s] = node_pool;
                    }
                }
                co_return true;
            }
        }
        co_return false;
    }

    core::Task<RespValue> execute(std::string_view key, const std::vector<std::string_view>& args) {
        uint16_t slot = key_slot(key);
        auto pool = slots_[slot];
        if (!pool) {
            co_await refresh_slots();
            pool = slots_[slot];
            if (!pool && !config_.seed_nodes.empty()) {
                pool = get_or_create_pool(config_.seed_nodes[0].first, config_.seed_nodes[0].second);
            }
        }

        if (!pool) {
            RespValue err;
            err.type = RespType::Error;
            err.data = std::string("Cluster topology not initialized");
            co_return err;
        }

        auto resp = co_await pool->execute(args);

        // Handle MOVED redirect
        if (resp.is_error() && resp.as_string().starts_with("MOVED ")) {
            // Format: MOVED <slot> <ip>:<port>
            std::string_view err_str = resp.as_string();
            err_str.remove_prefix(6); // remove "MOVED "
            size_t space_pos = err_str.find(' ');
            if (space_pos != std::string_view::npos) {
                std::string_view target_addr = err_str.substr(space_pos + 1);
                size_t colon_pos = target_addr.find(':');
                if (colon_pos != std::string_view::npos) {
                    std::string target_ip(target_addr.substr(0, colon_pos));
                    uint16_t target_port = static_cast<uint16_t>(std::stoi(std::string(target_addr.substr(colon_pos + 1))));

                    auto new_pool = get_or_create_pool(target_ip, target_port);
                    slots_[slot] = new_pool;
                    co_return co_await new_pool->execute(args);
                }
            }
        }

        // Handle ASK redirect
        if (resp.is_error() && resp.as_string().starts_with("ASK ")) {
            std::string_view err_str = resp.as_string();
            err_str.remove_prefix(4); // remove "ASK "
            size_t space_pos = err_str.find(' ');
            if (space_pos != std::string_view::npos) {
                std::string_view target_addr = err_str.substr(space_pos + 1);
                size_t colon_pos = target_addr.find(':');
                if (colon_pos != std::string_view::npos) {
                    std::string target_ip(target_addr.substr(0, colon_pos));
                    uint16_t target_port = static_cast<uint16_t>(std::stoi(std::string(target_addr.substr(colon_pos + 1))));

                    auto ask_pool = get_or_create_pool(target_ip, target_port);
                    auto guard = co_await ask_pool->acquire();
                    co_await guard->execute({"ASKING"});
                    co_return co_await guard->execute(args);
                }
            }
        }

        co_return resp;
    }

private:
    core::IoUring& ring_;
    ClusterConfig config_;
    std::array<std::shared_ptr<RedisConnectionPool>, 16384> slots_{};
    std::unordered_map<std::string, std::shared_ptr<RedisConnectionPool>> node_pools_;
};

} // namespace aegon::data::redis
