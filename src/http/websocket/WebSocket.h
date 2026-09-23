#pragma once

#include "http/websocket/WebSocketFrame.h"
#include "http/websocket/WebSocketTransport.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <span>
#include <memory>
#include <optional>
#include <functional>
#include <queue>
#include <coroutine>

namespace aegon::http::websocket {

class Message {
public:
    Message(std::string_view payload, Opcode op)
        : payload_(payload), opcode_(op) {}

    [[nodiscard]] std::string_view payload() const noexcept { return payload_; }
    [[nodiscard]] std::string_view text() const noexcept { return payload_; }
    [[nodiscard]] std::span<const uint8_t> binary() const noexcept {
        return {reinterpret_cast<const uint8_t*>(payload_.data()), payload_.size()};
    }
    [[nodiscard]] Opcode opcode() const noexcept { return opcode_; }
    [[nodiscard]] bool is_text() const noexcept { return opcode_ == Opcode::Text; }
    [[nodiscard]] bool is_binary() const noexcept { return opcode_ == Opcode::Binary; }

private:
    std::string_view payload_;
    Opcode opcode_;
};

class WebSocket {
public:
    using MessageCallback = std::function<core::Task<void>(WebSocket&, Message)>;
    using TextCallback = std::function<core::Task<void>(WebSocket&, std::string_view)>;
    using BinaryCallback = std::function<core::Task<void>(WebSocket&, std::span<const uint8_t>)>;
    using CloseCallback = std::function<core::Task<void>(WebSocket&, CloseCode, std::string_view)>;
    using PingCallback = std::function<core::Task<void>(WebSocket&, std::string_view)>;
    using ErrorCallback = std::function<void(WebSocket&, const std::exception&)>;

    explicit WebSocket(IWebSocketTransport& transport, std::string path = "", int fd = -1)
        : transport_(&transport), path_(std::move(path)), fd_(fd), open_(true) {}

    virtual ~WebSocket() = default;

    // Non-copyable
    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;
    WebSocket(WebSocket&&) = default;
    WebSocket& operator=(WebSocket&&) = default;

    [[nodiscard]] bool is_open() const noexcept {
        return open_ && transport_ && transport_->is_open();
    }

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] std::string_view path() const noexcept { return path_; }
    [[nodiscard]] TransportProtocol transport_protocol() const noexcept {
        return transport_ ? transport_->protocol() : TransportProtocol::Http1;
    }

    /**
     * @brief Sends an unmasked WebSocket frame over the underlying transport.
     */
    core::Task<void> send(std::string_view payload, Opcode op = Opcode::Text) {
        if (!is_open()) co_return;

        std::array<uint8_t, 10> header{};
        size_t hlen = serialize_frame_header(op, payload.size(), header.data(), true);

        (void)co_await transport_->write_frame({header.data(), hlen}, payload);
    }

    core::Task<void> send_text(std::string_view text) {
        return send(text, Opcode::Text);
    }

    core::Task<void> send_binary(std::span<const uint8_t> data) {
        return send({reinterpret_cast<const char*>(data.data()), data.size()}, Opcode::Binary);
    }

    core::Task<void> send_ping(std::string_view payload = "") {
        return send(payload, Opcode::Ping);
    }

    core::Task<void> send_pong(std::string_view payload = "") {
        return send(payload, Opcode::Pong);
    }

    /**
     * @brief Initiates graceful WebSocket closing handshake.
     */
    core::Task<void> close(CloseCode code = CloseCode::Normal, std::string_view reason = "") {
        if (!open_) co_return;
        open_ = false;

        std::string close_payload;
        close_payload.resize(2 + reason.size());
        uint16_t be_code = htobe16(static_cast<uint16_t>(code));
        std::memcpy(close_payload.data(), &be_code, 2);
        if (!reason.empty()) {
            std::memcpy(close_payload.data() + 2, reason.data(), reason.size());
        }

        std::array<uint8_t, 10> header{};
        size_t hlen = serialize_frame_header(Opcode::Close, close_payload.size(), header.data(), true);

        (void)co_await transport_->write_frame({header.data(), hlen}, close_payload);
        co_await transport_->close_transport();
    }

    // Event Registration
    void on_message(MessageCallback cb) { message_cb_ = std::move(cb); }
    void on_text(TextCallback cb) { text_cb_ = std::move(cb); }
    void on_binary(BinaryCallback cb) { binary_cb_ = std::move(cb); }
    void on_close(CloseCallback cb) { close_cb_ = std::move(cb); }
    void on_ping(PingCallback cb) { ping_cb_ = std::move(cb); }
    void on_error(ErrorCallback cb) { error_cb_ = std::move(cb); }

    // Internal dispatch helpers invoked by connection loop
    [[nodiscard]] const MessageCallback& message_callback() const noexcept { return message_cb_; }
    [[nodiscard]] const TextCallback& text_callback() const noexcept { return text_cb_; }
    [[nodiscard]] const BinaryCallback& binary_callback() const noexcept { return binary_cb_; }
    [[nodiscard]] const CloseCallback& close_callback() const noexcept { return close_cb_; }
    [[nodiscard]] const PingCallback& ping_callback() const noexcept { return ping_cb_; }
    [[nodiscard]] const ErrorCallback& error_callback() const noexcept { return error_cb_; }

    void mark_closed() noexcept { open_ = false; }

private:
    IWebSocketTransport* transport_{nullptr};
    std::string path_;
    int fd_{-1};
    bool open_{false};

    MessageCallback message_cb_{nullptr};
    TextCallback text_cb_{nullptr};
    BinaryCallback binary_cb_{nullptr};
    CloseCallback close_cb_{nullptr};
    PingCallback ping_cb_{nullptr};
    ErrorCallback error_cb_{nullptr};
};

using WebSocketHandler = std::function<core::Task<void>(WebSocket&)>;
using WebSocketEchoHandler = std::function<core::Task<void>(WebSocket&, Message)>;

} // namespace aegon::http::websocket
