#pragma once

#include "net/quic/QuicConnection.h"
#include "net/quic/QuicPacket.h"
#include "net/udp/UdpSocket.h"
#include "net/tls/TlsContext.h"
#include "core/EventLoop.h"
#include "core/Task.h"
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>
#include <string>

namespace aegon::net::quic {

/**
 * @brief High-performance standalone QUIC server (RFC 9000, RFC 9002) over UDP without HTTP.
 */
class QuicServer {
public:
    using StreamHandler = std::function<void(QuicConnection& conn, int64_t stream_id, std::span<const uint8_t> data, bool fin)>;
    using ConnectionHandler = std::function<void(QuicConnection& conn)>;

    QuicServer(core::EventLoop& loop, uint16_t port, tls::TlsContext& tls_ctx, std::string_view host = "0.0.0.0");
    ~QuicServer();

    QuicServer(const QuicServer&) = delete;
    QuicServer& operator=(const QuicServer&) = delete;

    QuicServer& on_connection(ConnectionHandler handler) {
        conn_handler_ = std::move(handler);
        return *this;
    }

    QuicServer& on_stream_data(StreamHandler handler) {
        stream_handler_ = std::move(handler);
        return *this;
    }

    bool start();
    void stop() noexcept;

    [[nodiscard]] bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] uint16_t port() const noexcept { return port_; }
    [[nodiscard]] size_t connection_count() const noexcept { return connections_.size(); }

private:
    core::Task<void> run_receive_loop();
    core::Task<void> run_timer_loop();

    void dispatch_datagram(std::span<const uint8_t> pkt, const sockaddr_storage& from, socklen_t from_len);

    core::EventLoop& loop_;
    uint16_t port_{0};
    std::string host_{"0.0.0.0"};
    tls::TlsContext& tls_ctx_;
    UdpSocket socket_{};
    std::atomic<bool> running_{false};

    ConnectionHandler conn_handler_{};
    StreamHandler stream_handler_{};

    std::unordered_map<std::string, std::shared_ptr<QuicConnection>> connections_{};
};

} // namespace aegon::net::quic

