#pragma once

#include <string>
#include <deque>
#include <optional>
#include <coroutine>
#include <cstring>
#include <arpa/inet.h>
#include "core/Task.h"

namespace aegon::http::grpc {

/**
 * @brief Inbound message channel for Client Streaming and Bidirectional Streaming RPCs.
 *
 * Buffers incoming HTTP/2 DATA chunks and extracts complete 5-byte framed gRPC messages.
 * Allows handlers to asynchronously co_await channel.read() with zero busy-waiting.
 */
class GrpcInboundChannel {
private:
    std::string buffer_{};
    std::deque<std::string> messages_{};
    bool eof_{false};
    std::coroutine_handle<> waiter_{nullptr};

    void extract_messages() {
        while (buffer_.size() >= 5) {
            uint32_t net_len = 0;
            std::memcpy(&net_len, buffer_.data() + 1, 4);
            uint32_t msg_len = ntohl(net_len);

            if (buffer_.size() < 5 + msg_len) {
                break; // Incomplete message; wait for more network frames
            }

            messages_.push_back(buffer_.substr(0, 5 + msg_len));
            buffer_.erase(0, 5 + msg_len);
        }

        if (waiter_ && (!messages_.empty() || eof_)) {
            auto h = waiter_;
            waiter_ = nullptr;
            h.resume();
        }
    }

public:
    GrpcInboundChannel() = default;

    void push_data(const uint8_t* data, size_t len) {
        buffer_.append(reinterpret_cast<const char*>(data), len);
        extract_messages();
    }

    void set_eof() {
        eof_ = true;
        extract_messages();
        if (waiter_) {
            auto h = waiter_;
            waiter_ = nullptr;
            h.resume();
        }
    }

    [[nodiscard]] bool is_eof() const noexcept {
        return eof_ && messages_.empty() && buffer_.empty();
    }

    struct ReadAwaiter {
        GrpcInboundChannel& channel;

        [[nodiscard]] bool await_ready() const noexcept {
            return !channel.messages_.empty() || channel.eof_;
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            channel.waiter_ = handle;
        }

        std::optional<std::string> await_resume() noexcept {
            if (!channel.messages_.empty()) {
                std::string msg = std::move(channel.messages_.front());
                channel.messages_.pop_front();
                return msg;
            }
            return std::nullopt;
        }
    };

    ReadAwaiter read_raw() {
        return ReadAwaiter{*this};
    }
};

} // namespace aegon::http::grpc
