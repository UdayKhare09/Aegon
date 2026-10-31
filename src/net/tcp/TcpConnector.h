#pragma once

#include "core/engine/IoEngine.h"
#include "core/Task.h"
#include "net/SocketAddress.h"
#include "net/tcp/TcpStream.h"
#include <string_view>
#include <optional>
#include <chrono>

namespace aegon::net {

/**
 * @brief High-performance asynchronous non-blocking TCP client connector.
 */
class TcpConnector {
public:
    /**
     * @brief Connects asynchronously to a known SocketAddress with optional timeout.
     */
    [[nodiscard]] static core::Task<std::optional<TcpStream>> connect(
        core::IoEngine& engine,
        const SocketAddress& addr,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    /**
     * @brief Resolves hostname, attempts connection across candidate addresses (Happy Eyeballs / dual stack).
     */
    [[nodiscard]] static core::Task<std::optional<TcpStream>> connect(
        core::IoEngine& engine,
        std::string_view host,
        uint16_t port,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));
};

} // namespace aegon::net

