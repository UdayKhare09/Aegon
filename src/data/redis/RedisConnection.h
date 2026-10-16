#pragma once

#include "Resp3.h"
#include "core/OsSocket.h"
#include "core/engine/IoEngine.h"
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
    RedisConnection(core::IoEngine& engine, RedisNodeConfig config);
    ~RedisConnection();

    RedisConnection(const RedisConnection&) = delete;
    RedisConnection& operator=(const RedisConnection&) = delete;
    RedisConnection(RedisConnection&&) noexcept;
    RedisConnection& operator=(RedisConnection&&) noexcept;

    core::Task<bool> connect();
    core::Task<RespValue> execute(const std::vector<std::string_view>& args);
    core::Task<std::vector<RespValue>> execute_pipeline(const std::vector<std::vector<std::string_view>>& batch);

    void close();
    [[nodiscard]] bool is_connected() const noexcept { return fd_ != core::INVALID_OS_SOCKET; }
    [[nodiscard]] core::os_socket_t fd() const noexcept { return fd_; }
    [[nodiscard]] const RedisNodeConfig& config() const noexcept { return config_; }

private:
    core::Task<RespValue> read_response();

    core::IoEngine& engine_;
    RedisNodeConfig config_;
    core::os_socket_t fd_{core::INVALID_OS_SOCKET};
    std::string read_buffer_;
};

} // namespace aegon::data::redis
