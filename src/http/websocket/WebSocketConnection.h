#pragma once

#include "http/websocket/WebSocket.h"
#include "http/websocket/WebSocketFrame.h"
#include "http/websocket/WebSocketTransport.h"
#include "http/websocket/WebSocketSession.h"
#include "core/EventLoop.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <span>

namespace aegon::http::websocket {

/**
 * @brief Manages the socket lifecycle and frame I/O for an HTTP/1.1 WebSocket connection.
 * Implements IWebSocketTransport over io_uring sockets and delegates frame logic to WebSocketSession.
 */
class WebSocketConnection : public IWebSocketTransport {
public:
    WebSocketConnection(core::EventLoop& loop, int client_fd, std::string path,
                        WebSocketHandler handler = nullptr,
                        WebSocketEchoHandler echo_handler = nullptr);

    ~WebSocketConnection() override = default;

    [[nodiscard]] TransportProtocol protocol() const noexcept override {
        return TransportProtocol::Http1;
    }

    [[nodiscard]] bool is_open() const noexcept override {
        return is_open_ && client_fd_ >= 0;
    }

    core::Task<int> write_frame(std::span<const uint8_t> header, std::string_view payload) override;
    core::Task<int> write_raw(std::string_view bytes) override;
    core::Task<void> close_transport() override;

    /**
     * @brief Starts the io_uring multishot receive and frame processing loop.
     * @param initial_data Any trailing bytes left in the HTTP receive buffer after handshake.
     */
    core::Task<void> run(std::string initial_data = "");

private:
    core::EventLoop& loop_;
    int client_fd_{-1};
    bool is_open_{true};
    WebSocketSession session_;
};

} // namespace aegon::http::websocket
