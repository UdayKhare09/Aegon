#pragma once

#include "http/websocket/WebSocket.h"
#include "http/websocket/WebSocketFrame.h"
#include "http/websocket/WebSocketTransport.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <span>

namespace aegon::http::websocket {

/**
 * @brief Transport-agnostic RFC 6455 frame parser and session dispatcher.
 * Shared across HTTP/1.1 (TCP sockets), HTTP/2 (RFC 8441 streams), and HTTP/3 (RFC 9220 streams).
 */
class WebSocketSession {
public:
    WebSocketSession(IWebSocketTransport& transport, std::string path = "",
                     WebSocketHandler handler = nullptr,
                     WebSocketEchoHandler echo_handler = nullptr);

    WebSocket& websocket() noexcept { return ws_; }
    const WebSocket& websocket() const noexcept { return ws_; }

    [[nodiscard]] bool is_closed() const noexcept { return is_closed_ || !ws_.is_open(); }

    /**
     * @brief Feeds incoming raw bytes received from transport.
     * Parses pipelined frames, unmasks payloads with AVX2 SIMD, and dispatches.
     * @return true if session remains active, false if closed or protocol error.
     */
    core::Task<bool> process_incoming_data(std::string_view data);

private:
    IWebSocketTransport& transport_;
    WebSocket ws_;
    WebSocketHandler handler_{nullptr};
    WebSocketEchoHandler echo_handler_{nullptr};
    std::string stream_buf_;
    std::string batch_out_;
    bool is_closed_{false};
    bool initialized_{false};
};

} // namespace aegon::http::websocket
