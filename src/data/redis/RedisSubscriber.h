#pragma once

#include "RedisConnection.h"
#include "core/IoUring.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <memory>

namespace aegon::data::redis {

struct RedisMessage {
    std::string channel;
    std::string pattern;
    std::string payload;
};

class RedisSubscriber {
public:
    RedisSubscriber(core::IoUring& ring, RedisNodeConfig config)
        : ring_(ring), config_(std::move(config)), conn_(ring_, config_) {}

    core::Task<bool> connect() {
        co_return co_await conn_.connect();
    }

    core::Task<bool> subscribe(const std::vector<std::string_view>& channels) {
        if (!conn_.is_connected()) {
            bool ok = co_await connect();
            if (!ok) co_return false;
        }

        std::vector<std::string_view> cmd;
        cmd.reserve(channels.size() + 1);
        cmd.push_back("SUBSCRIBE");
        for (auto ch : channels) cmd.push_back(ch);
        auto res = co_await conn_.execute(cmd);
        co_return !res.is_error();
    }

    core::Task<bool> unsubscribe(const std::vector<std::string_view>& channels) {
        if (!conn_.is_connected()) co_return false;
        std::vector<std::string_view> cmd;
        cmd.reserve(channels.size() + 1);
        cmd.push_back("UNSUBSCRIBE");
        for (auto ch : channels) cmd.push_back(ch);
        auto res = co_await conn_.execute(cmd);
        co_return !res.is_error();
    }

    core::Task<bool> psubscribe(const std::vector<std::string_view>& patterns) {
        if (!conn_.is_connected()) {
            bool ok = co_await connect();
            if (!ok) co_return false;
        }

        std::vector<std::string_view> cmd;
        cmd.reserve(patterns.size() + 1);
        cmd.push_back("PSUBSCRIBE");
        for (auto pat : patterns) cmd.push_back(pat);
        auto res = co_await conn_.execute(cmd);
        co_return !res.is_error();
    }

    core::Task<std::optional<RedisMessage>> next_message() {
        while (conn_.is_connected()) {
            auto res = co_await conn_.execute({});
            if (res.is_array()) {
                const auto& arr = res.as_array();
                if (arr.size() >= 3 && arr[0].as_string() == "message") {
                    co_return RedisMessage{
                        .channel = std::string(arr[1].as_string()),
                        .pattern = "",
                        .payload = std::string(arr[2].as_string())
                    };
                } else if (arr.size() >= 4 && arr[0].as_string() == "pmessage") {
                    co_return RedisMessage{
                        .channel = std::string(arr[2].as_string()),
                        .pattern = std::string(arr[1].as_string()),
                        .payload = std::string(arr[3].as_string())
                    };
                }
            } else if (res.is_error()) {
                break;
            }
        }
        co_return std::nullopt;
    }

    void close() {
        conn_.close();
    }

    [[nodiscard]] bool is_connected() const noexcept {
        return conn_.is_connected();
    }

private:
    core::IoUring& ring_;
    RedisNodeConfig config_;
    RedisConnection conn_;
};

} // namespace aegon::data::redis
