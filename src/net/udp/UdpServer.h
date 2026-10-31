#pragma once

#include "net/udp/UdpSocket.h"
#include "core/EventLoop.h"
#include "core/Task.h"
#include <functional>
#include <atomic>
#include <memory>
#include <span>

namespace aegon::net {

/**
 * @brief High-level asynchronous UDP server with coroutine datagram dispatch.
 */
class UdpServer {
public:
    using PacketHandler = std::function<core::Task<void>(std::span<const uint8_t> data, const SocketAddress& from, UdpSocket& socket)>;

    UdpServer(core::EventLoop& loop, uint16_t port, std::string_view host = "0.0.0.0", bool reuse_port = true)
        : loop_(loop), port_(port), host_(host), reuse_port_(reuse_port) {}

    ~UdpServer() {
        stop();
    }

    UdpServer(const UdpServer&) = delete;
    UdpServer& operator=(const UdpServer&) = delete;

    UdpServer& on_packet(PacketHandler handler) {
        handler_ = std::move(handler);
        return *this;
    }

    bool start() {
        auto sock = UdpSocket::bind(port_, host_, reuse_port_);
        if (!sock) {
            return false;
        }
        socket_ = std::move(*sock);
        running_ = true;
        loop_.spawn(receive_loop());
        return true;
    }

    void stop() noexcept {
        if (running_.exchange(false)) {
            socket_.close();
        }
    }

    [[nodiscard]] bool is_running() const noexcept { return running_; }
    [[nodiscard]] uint16_t port() const noexcept { return socket_.local_address().port(); }
    [[nodiscard]] UdpSocket& socket() noexcept { return socket_; }

private:
    core::Task<void> receive_loop() {
        alignas(64) uint8_t buf[65536];
        SocketAddress from_addr;

        while (running_ && socket_.is_valid()) {
            int n = co_await socket_.recvfrom(loop_.engine(), buf, sizeof(buf), from_addr);
            if (n <= 0) {
                if (!running_) break;
                continue;
            }

            if (handler_) {
                loop_.spawn(handler_(std::span<const uint8_t>(buf, static_cast<size_t>(n)), from_addr, socket_));
            }
        }
    }

    core::EventLoop& loop_;
    uint16_t port_{0};
    std::string host_{"0.0.0.0"};
    bool reuse_port_{true};
    UdpSocket socket_{};
    std::atomic<bool> running_{false};
    PacketHandler handler_{};
};

} // namespace aegon::net

