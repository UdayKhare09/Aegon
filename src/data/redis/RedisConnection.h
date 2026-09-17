#pragma once

#include "Resp3.h"
#include "core/IoUring.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <chrono>

namespace aegon::data::redis {

struct RedisNodeConfig {
    std::string host{"127.0.0.1"};
    uint16_t port{6379};
    std::string username;
    std::string password;
    uint32_t database{0};
    std::chrono::milliseconds connect_timeout{2000};
};

class RedisConnection {
public:
    RedisConnection(core::IoUring& ring, RedisNodeConfig config);
    ~RedisConnection();

    RedisConnection(const RedisConnection&) = delete;
    RedisConnection& operator=(const RedisConnection&) = delete;
    RedisConnection(RedisConnection&&) noexcept;
    RedisConnection& operator=(RedisConnection&&) noexcept;

    core::Task<bool> connect();
    core::Task<RespValue> execute(const std::vector<std::string_view>& args);
    core::Task<std::vector<RespValue>> execute_pipeline(const std::vector<std::vector<std::string_view>>& batch);

    void close();
    [[nodiscard]] bool is_connected() const noexcept { return fd_ >= 0; }
    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] const RedisNodeConfig& config() const noexcept { return config_; }

private:
    core::Task<RespValue> read_response();

    core::IoUring& ring_;
    RedisNodeConfig config_;
    int fd_{-1};
    std::string read_buffer_;
};

} // namespace aegon::data::redis
