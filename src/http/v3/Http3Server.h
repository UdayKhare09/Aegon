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
    core::Task<void> dispatch_datagram(std::span<const uint8_t> pkt,
                                       const sockaddr_storage& from_addr,
                                       socklen_t from_len);

    core::EventLoop& loop_;
    uint16_t port_;
    const Router& router_;
    SSL_CTX* ssl_ctx_{nullptr};
    const ServiceRegistry* services_{nullptr};

    int udp_fd_{-1};
    bool running_{false};

    struct CidHash {
        using is_transparent = void;
        size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
        size_t operator()(const std::string& s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };

    struct CidEqual {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept {
            return a == b;
        }
    };

    // Map DCID string -> Http3Connection with transparent hashing for zero-allocation lookup
    std::unordered_map<std::string, std::shared_ptr<Http3Connection>, CidHash, CidEqual> connections_;
};

} // namespace aegon::http::v3
