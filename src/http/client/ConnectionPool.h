#pragma once

#include "core/EventLoop.h"
#include "http/client/Url.h"
#include "http/client/TlsClientStream.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <expected>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>

namespace aegon::http::client {

enum class ConnectionError {
    DnsLookupFailed,
    SocketCreationFailed,
    ConnectFailed,
    TlsHandshakeFailed,
    Timeout,
    Closed
};

/**
 * @brief Represents an active or pooled HTTP/1.1 or HTTP/2 client connection.
 */
class PooledConnection {
public:
    PooledConnection(core::EventLoop& loop, int fd, std::string origin, 
                     std::unique_ptr<TlsClientStream> tls = nullptr);
    ~PooledConnection();

    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;
    PooledConnection(PooledConnection&&) noexcept;
    PooledConnection& operator=(PooledConnection&&) noexcept;

    core::Task<int> write(const void* buf, size_t len);
    core::Task<int> read(void* buf, size_t max_len);

    void close() noexcept;

    void set_loop(core::EventLoop& loop) noexcept {
        loop_ = &loop;
        if (tls_stream_) {
            tls_stream_->set_loop(loop);
        }
    }

    [[nodiscard]] bool is_valid() const noexcept { return fd_ >= 0 && !is_closed_; }
    [[nodiscard]] bool is_tls() const noexcept { return tls_stream_ != nullptr; }
    [[nodiscard]] const std::string& origin() const noexcept { return origin_; }
    [[nodiscard]] std::string_view alpn() const noexcept {
        return tls_stream_ ? tls_stream_->alpn() : "";
    }
    [[nodiscard]] bool is_h2() const noexcept {
        return tls_stream_ ? tls_stream_->is_h2() : false;
    }

    core::EventLoop* loop_{nullptr};
    int fd_{-1};
    std::string origin_{};
    std::unique_ptr<TlsClientStream> tls_stream_{nullptr};
    bool is_closed_{false};

public:
    std::chrono::steady_clock::time_point last_used;
};

struct PoolConfig {
    size_t max_connections_per_host{32};
    size_t max_idle_connections{128};
    std::chrono::seconds idle_timeout{60};
    std::chrono::milliseconds connect_timeout{5000};
};

/**
 * @brief Manages persistent keep-alive TCP/TLS connections to remote hosts.
 */
class ConnectionPool {
public:
    ConnectionPool(PoolConfig config, TlsClientOptions tls_options);
    ~ConnectionPool();

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
    ConnectionPool(ConnectionPool&&) noexcept = default;
    ConnectionPool& operator=(ConnectionPool&&) noexcept = default;

    /**
     * @brief Acquires an existing idle connection or opens a new one asynchronously.
     */
    core::Task<std::expected<std::unique_ptr<PooledConnection>, ConnectionError>> 
    acquire(const Url& url, core::EventLoop& loop);

    /**
     * @brief Returns a connection to the pool or closes it if keep_alive is false.
     */
    void release(std::unique_ptr<PooledConnection> conn, bool keep_alive);

    /**
     * @brief Closes all idle connections in the pool.
     */
    void clear();

    [[nodiscard]] size_t idle_count() const noexcept;

private:
    core::Task<std::expected<std::unique_ptr<PooledConnection>, ConnectionError>> 
    create_connection(const Url& url, core::EventLoop& loop);

    PoolConfig config_;
    TlsClientOptions tls_options_;
    TlsClientContext tls_ctx_;
    std::unordered_map<std::string, std::vector<std::unique_ptr<PooledConnection>>> idle_conns_;
};

} // namespace aegon::http::client
