#pragma once

#include "RedisConnectionPool.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>

namespace aegon::data::redis {

class RedisTransaction {
public:
    explicit RedisTransaction(RedisConnectionPool::Guard guard)
        : guard_(std::move(guard)) {}

    static core::Task<std::optional<RedisTransaction>> begin(RedisConnectionPool& pool) {
        auto guard = co_await pool.acquire();
        if (!guard.conn) {
            co_return std::nullopt;
        }

        auto res = co_await guard->execute({"MULTI"});
        if (res.is_error()) {
            co_return std::nullopt;
        }

        co_return RedisTransaction(std::move(guard));
    }

    RedisTransaction& set(std::string_view key, std::string_view val) {
        commands_.push_back(copy_cmd({"SET", key, val}));
        return *this;
    }

    RedisTransaction& get(std::string_view key) {
        commands_.push_back(copy_cmd({"GET", key}));
        return *this;
    }

    RedisTransaction& incr(std::string_view key) {
        commands_.push_back(copy_cmd({"INCR", key}));
        return *this;
    }

    RedisTransaction& del(std::string_view key) {
        commands_.push_back(copy_cmd({"DEL", key}));
        return *this;
    }

    RedisTransaction& command(std::vector<std::string_view> cmd) {
        commands_.push_back(copy_cmd(cmd));
        return *this;
    }

    core::Task<std::vector<RespValue>> exec() {
        if (!guard_.conn) {
            co_return std::vector<RespValue>{};
        }

        for (const auto& cmd : commands_) {
            std::vector<std::string_view> views;
            views.reserve(cmd.size());
            for (const auto& s : cmd) views.push_back(s);
            auto queue_res = co_await guard_->execute(views);
            if (queue_res.is_error()) {
                co_await guard_->execute({"DISCARD"});
                co_return std::vector<RespValue>{};
            }
        }

        auto exec_res = co_await guard_->execute({"EXEC"});
        if (exec_res.is_array()) {
            co_return exec_res.as_array();
        }
        co_return std::vector<RespValue>{};
    }

    core::Task<bool> discard() {
        if (!guard_.conn) co_return false;
        auto res = co_await guard_->execute({"DISCARD"});
        co_return !res.is_error();
    }

private:
    std::vector<std::string> copy_cmd(const std::vector<std::string_view>& cmd) {
        std::vector<std::string> res;
        res.reserve(cmd.size());
        for (auto sv : cmd) res.emplace_back(sv);
        return res;
    }

    RedisConnectionPool::Guard guard_;
    std::vector<std::vector<std::string>> commands_;
};

} // namespace aegon::data::redis
