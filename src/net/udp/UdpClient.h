#pragma once

#include "net/udp/UdpSocket.h"
#include "core/EventLoop.h"
#include "core/Task.h"
#include <chrono>
#include <optional>
#include <span>
#include <vector>

namespace aegon::net {

/**
 * @brief High-level asynchronous UDP client utility.
 */
class UdpClient {
public:
    UdpClient() = default;

    static std::optional<UdpClient> create(uint16_t bind_port = 0, std::string_view bind_host = "0.0.0.0") {
        auto sock = UdpSocket::bind(bind_port, bind_host, false);
        if (!sock) return std::nullopt;
        UdpClient client;
        client.socket_ = std::move(*sock);
        return client;
    }

    [[nodiscard]] UdpSocket& socket() noexcept { return socket_; }

    [[nodiscard]] int send(std::span<const uint8_t> data, const SocketAddress& to) noexcept {
        return socket_.sendto(data.data(), data.size(), to);
    }

    [[nodiscard]] core::Task<std::optional<std::vector<uint8_t>>> send_and_receive(
        core::IoEngine& engine,
        std::span<const uint8_t> data,
        const SocketAddress& to,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
        (void)timeout;

        int sent = socket_.sendto(data.data(), data.size(), to);
        if (sent <= 0) co_return std::nullopt;

        alignas(64) uint8_t buf[65536];
        SocketAddress from;
        int n = co_await socket_.recvfrom(engine, buf, sizeof(buf), from);
        if (n <= 0) co_return std::nullopt;

        co_return std::vector<uint8_t>(buf, buf + n);
    }

    void close() noexcept {
        socket_.close();
    }

private:
    UdpSocket socket_{};
};

} // namespace aegon::net

