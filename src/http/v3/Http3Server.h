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
                SSL_CTX* ssl_ctx, void* user_state = nullptr);
    ~Http3Server();

    Http3Server(const Http3Server&) = delete;
    Http3Server& operator=(const Http3Server&) = delete;

    /**
     * @brief Bind UDP socket and initialize server.
     */
    bool start();

    /**
     * @brief Stop the server and close UDP socket.
     */
    void stop() noexcept;

    /**
     * @brief Coroutine receive loop handling inbound UDP datagrams.
     */
    core::Task<void> run_receive_loop();

    [[nodiscard]] int socket_fd() const noexcept { return udp_fd_; }
    [[nodiscard]] uint16_t port() const noexcept { return port_; }
    [[nodiscard]] size_t connection_count() const noexcept { return connections_.size(); }

private:
    core::EventLoop& loop_;
    uint16_t port_;
    const Router& router_;
    SSL_CTX* ssl_ctx_{nullptr};
    void* user_state_{nullptr};

    int udp_fd_{-1};
    bool running_{false};

    // Map DCID string -> Http3Connection
    std::unordered_map<std::string, std::shared_ptr<Http3Connection>> connections_;
};

} // namespace aegon::http::v3
