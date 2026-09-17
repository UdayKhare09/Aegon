#pragma once

#include "RedisConnection.h"
#include "core/IoUring.h"
#include "core/Task.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace aegon::data::redis {

struct SentinelConfig {
    std::string master_name{"mymaster"};
    std::vector<std::pair<std::string, uint16_t>> sentinels{{"127.0.0.1", 26379}};
    std::string username;
    std::string password;
    uint32_t database{0};
};

class RedisSentinelResolver {
public:
    RedisSentinelResolver(core::IoUring& ring, SentinelConfig config)
        : ring_(ring), config_(std::move(config)) {}

    core::Task<std::optional<RedisNodeConfig>> discover_master() {
        for (const auto& [shost, sport] : config_.sentinels) {
            RedisNodeConfig sconfig{
                .host = shost,
                .port = sport
            };
            RedisConnection sconn(ring_, sconfig);
            bool ok = co_await sconn.connect();
            if (!ok) continue;

            auto resp = co_await sconn.execute({"SENTINEL", "get-master-addr-by-name", config_.master_name});
            if (resp.is_array() && resp.as_array().size() >= 2) {
                const auto& arr = resp.as_array();
                std::string master_host = std::string(arr[0].as_string());
                uint16_t master_port = static_cast<uint16_t>(std::stoi(std::string(arr[1].as_string())));

                RedisNodeConfig master_cfg{
                    .host = std::move(master_host),
                    .port = master_port,
                    .password = config_.password,
                    .database = config_.database
                };
                co_return master_cfg;
            }
        }
        co_return std::nullopt;
    }

private:
    core::IoUring& ring_;
    SentinelConfig config_;
};

} // namespace aegon::data::redis
