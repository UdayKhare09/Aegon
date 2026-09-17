#pragma once

#include "RedisConnectionPool.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>

namespace aegon::data::redis {

class RedisPipeline {
public:
    explicit RedisPipeline(RedisConnectionPool& pool)
        : pool_(pool) {}

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

        auto guard = co_await pool_.acquire();
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

    RedisConnectionPool& pool_;
    std::vector<std::vector<std::string>> commands_;
    std::vector<std::string> ttl_storage_;
};

} // namespace aegon::data::redis
