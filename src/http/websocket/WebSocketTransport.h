#pragma once

#include "core/Task.h"
#include <cstdint>
#include <string_view>
#include <span>

namespace aegon::http::websocket {

enum class TransportProtocol : uint8_t {
    Http1, // RFC 6455 over TCP / TLS socket
    Http2, // RFC 8441 Extended CONNECT over HTTP/2 stream
    Http3  // RFC 9220 Extended CONNECT over HTTP/3 / QUIC stream
};

/**
 * @brief Protocol-agnostic transport interface for WebSocket connections.
 * Decouples WebSocket frame processing from the underlying HTTP transport (H1, H2, or H3).
 */
class IWebSocketTransport {
public:
    virtual ~IWebSocketTransport() = default;

    [[nodiscard]] virtual TransportProtocol protocol() const noexcept = 0;
    [[nodiscard]] virtual bool is_open() const noexcept = 0;

    /**
     * @brief Writes a formatted WebSocket frame to the transport.
     * @param header Serialized RFC 6455 frame header.
     * @param payload Payload data to send.
     */
    virtual core::Task<int> write_frame(std::span<const uint8_t> header, std::string_view payload) = 0;

    /**
     * @brief Writes raw bytes (e.g. batched frames) directly to the transport.
     * @param bytes Serialized byte stream to send.
     */
    virtual core::Task<int> write_raw(std::string_view bytes) = 0;

    /**
     * @brief Closes the underlying transport channel (socket or stream).
     */
    virtual core::Task<void> close_transport() = 0;
};

} // namespace aegon::http::websocket
