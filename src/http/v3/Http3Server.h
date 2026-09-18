#pragma once

#include "http/v3/Http3Connection.h"
#include "http/v3/QuicPacket.h"
#include "http/Router.h"
#include "core/EventLoop.h"
#include "core/Task.h"
#include <openssl/ssl.h>
#include <unordered_map>
#include <memory>
#include <string>

namespace aegon::http::v3 {

class Http3Server {
public:
    Http3Server(core::EventLoop& loop, uint16_t port, const Router& router,
                SSL_CTX* ssl_ctx, const ServiceRegistry* services = nullptr);
    ~Http3Server();

    Http3Server(const Http3Server&) = delete;
    Http3Server& operator=(const Http3Server&) = delete;

    /**
     * @brief Bind UDP socket and initialize dual-stack server.
     */
    bool start();

    /**
     * @brief Stop the server, send GOAWAY to active connections, and close UDP socket.
     */
    void stop() noexcept;

    /**
     * @brief Coroutine receive loop handling inbound UDP datagrams.
     */
    core::Task<void> run_receive_loop();

    /**
     * @brief Coroutine timer loop handling QUIC loss recovery and timer ticks (RFC 9002).
     */
    core::Task<void> run_timer_loop();

    [[nodiscard]] int socket_fd() const noexcept { return udp_fd_; }
    [[nodiscard]] uint16_t port() const noexcept { return port_; }
    [[nodiscard]] size_t connection_count() const noexcept { return connections_.size(); }

    void check_expiries();
    void prune_connections();

private:
    void send_version_negotiation(const ngtcp2_version_cid& vc,
                                  const sockaddr_storage& remote_addr,
                                  socklen_t remote_addr_len);

    core::EventLoop& loop_;
    uint16_t port_;
    const Router& router_;
    SSL_CTX* ssl_ctx_{nullptr};
    const ServiceRegistry* services_{nullptr};

    int udp_fd_{-1};
    bool running_{false};

    // Map DCID string -> Http3Connection
    std::unordered_map<std::string, std::shared_ptr<Http3Connection>> connections_;
};

} // namespace aegon::http::v3
