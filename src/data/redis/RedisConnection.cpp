#include "RedisConnection.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

namespace aegon::data::redis {

RedisConnection::RedisConnection(core::IoUring& ring, RedisNodeConfig config)
    : ring_(ring), config_(std::move(config)) {}

RedisConnection::~RedisConnection() {
    close();
}

RedisConnection::RedisConnection(RedisConnection&& other) noexcept
    : ring_(other.ring_), config_(std::move(other.config_)), 
      fd_(other.fd_), read_buffer_(std::move(other.read_buffer_)) {
    other.fd_ = -1;
}

RedisConnection& RedisConnection::operator=(RedisConnection&& other) noexcept {
    if (this != &other) {
        close();
        config_ = std::move(other.config_);
        fd_ = other.fd_;
        read_buffer_ = std::move(other.read_buffer_);
        other.fd_ = -1;
    }
    return *this;
}

void RedisConnection::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    read_buffer_.clear();
}

core::Task<bool> RedisConnection::connect() {
    close();

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    std::string port_str = std::to_string(config_.port);
    int rc = getaddrinfo(config_.host.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        co_return false;
    }

    int sock = -1;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        sock = ::socket(p->ai_family, p->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, p->ai_protocol);
        if (sock < 0) continue;

        int flag = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        int conn_res = co_await ring_.connect(sock, p->ai_addr, p->ai_addrlen);
        if (conn_res >= 0) {
            fd_ = sock;
            break;
        }

        ::close(sock);
        sock = -1;
    }

    freeaddrinfo(res);

    if (fd_ < 0) {
        co_return false;
    }

    // Authenticate if password provided
    if (!config_.password.empty()) {
        RespValue auth_res;
        if (!config_.username.empty()) {
            auth_res = co_await execute({"AUTH", config_.username, config_.password});
        } else {
            auth_res = co_await execute({"AUTH", config_.password});
        }
        if (auth_res.is_error()) {
            close();
            co_return false;
        }
    }

    // Select database if not 0
    if (config_.database > 0) {
        std::string db_str = std::to_string(config_.database);
        auto sel_res = co_await execute({"SELECT", db_str});
        if (sel_res.is_error()) {
            close();
            co_return false;
        }
    }

    co_return true;
}

core::Task<RespValue> RedisConnection::read_response() {
    while (true) {
        if (!read_buffer_.empty()) {
            std::string_view sv = read_buffer_;
            RespValue val;
            ParseStatus status = Resp3Parser::parse(sv, val);
            if (status == ParseStatus::Done) {
                size_t consumed = read_buffer_.size() - sv.size();
                read_buffer_.erase(0, consumed);
                co_return val;
            } else if (status == ParseStatus::Error) {
                close();
                RespValue err;
                err.type = RespType::Error;
                err.data = std::string("RESP parse error");
                co_return err;
            }
        }

        // Need more data from socket
        char buf[4096];
        int bytes = co_await ring_.recv(fd_, buf, sizeof(buf), 0);
        if (bytes <= 0) {
            close();
            RespValue err;
            err.type = RespType::Error;
            err.data = std::string("Redis connection closed by remote host");
            co_return err;
        }

        read_buffer_.append(buf, static_cast<size_t>(bytes));
    }
}

core::Task<RespValue> RedisConnection::execute(const std::vector<std::string_view>& args) {
    if (!is_connected()) {
        bool ok = co_await connect();
        if (!ok) {
            RespValue err;
            err.type = RespType::Error;
            err.data = std::string("Failed to connect to Redis");
            co_return err;
        }
    }

    if (!args.empty()) {
        std::string wire_cmd = Resp3Serializer::serialize_command(args);
        int sent = co_await ring_.send(fd_, wire_cmd);
        if (sent <= 0) {
            close();
            RespValue err;
            err.type = RespType::Error;
            err.data = std::string("Failed to send command to Redis");
            co_return err;
        }
    }

    co_return co_await read_response();
}

core::Task<std::vector<RespValue>> RedisConnection::execute_pipeline(const std::vector<std::vector<std::string_view>>& batch) {
    std::vector<RespValue> results;
    if (batch.empty()) co_return results;

    if (!is_connected()) {
        bool ok = co_await connect();
        if (!ok) {
            results.resize(batch.size());
            for (auto& r : results) {
                r.type = RespType::Error;
                r.data = std::string("Failed to connect to Redis");
            }
            co_return results;
        }
    }

    std::string all_cmds;
    for (const auto& args : batch) {
        all_cmds.append(Resp3Serializer::serialize_command(args));
    }

    int sent = co_await ring_.send(fd_, all_cmds);
    if (sent <= 0) {
        close();
        results.resize(batch.size());
        for (auto& r : results) {
            r.type = RespType::Error;
            r.data = std::string("Failed to send pipeline commands to Redis");
        }
        co_return results;
    }

    results.reserve(batch.size());
    for (size_t i = 0; i < batch.size(); ++i) {
        results.push_back(co_await read_response());
    }

    co_return results;
}

} // namespace aegon::data::redis
