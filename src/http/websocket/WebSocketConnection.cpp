#include "http/websocket/WebSocketConnection.h"
#include <cerrno>
#include <cstring>

namespace aegon::http::websocket {

WebSocketConnection::WebSocketConnection(core::EventLoop& loop, int client_fd, std::string path,
                                         WebSocketHandler handler,
                                         WebSocketEchoHandler echo_handler)
    : loop_(loop),
      client_fd_(client_fd),
      session_(*this, std::move(path), std::move(handler), std::move(echo_handler))
{
}

core::Task<int> WebSocketConnection::write_frame(std::span<const uint8_t> header, std::string_view payload) {
    if (!is_open_ || client_fd_ < 0) co_return -1;

    if (header.size() + payload.size() <= 4096) {
        char buf[4096];
        if (!header.empty()) {
            std::memcpy(buf, header.data(), header.size());
        }
        if (!payload.empty()) {
            std::memcpy(buf + header.size(), payload.data(), payload.size());
        }
        co_return co_await loop_.ring().send_all(client_fd_, std::string_view(buf, header.size() + payload.size()));
    } else {
        std::string frame;
        frame.reserve(header.size() + payload.size());
        if (!header.empty()) {
            frame.append(reinterpret_cast<const char*>(header.data()), header.size());
        }
        frame.append(payload);
        co_return co_await loop_.ring().send_all(client_fd_, frame);
    }
}

core::Task<int> WebSocketConnection::write_raw(std::string_view bytes) {
    if (!is_open_ || client_fd_ < 0 || bytes.empty()) co_return -1;
    co_return co_await loop_.ring().send_all(client_fd_, bytes);
}

core::Task<void> WebSocketConnection::close_transport() {
    if (!is_open_) co_return;
    is_open_ = false;
    co_return;
}

core::Task<void> WebSocketConnection::run(std::string initial_data) {
    if (!initial_data.empty()) {
        bool ok = co_await session_.process_incoming_data(initial_data);
        if (!ok || session_.is_closed()) {
            is_open_ = false;
            (void)(co_await loop_.ring().close(client_fd_));
            client_fd_ = -1;
            co_return;
        }
    }

    while (is_open_ && !session_.is_closed()) {
        auto recv_res = co_await loop_.ring().recv_multishot(client_fd_, loop_.buffer_pool().bgid());
        if (recv_res.bytes == -ENOBUFS) {
            co_await loop_.ring().timeout(100'000ULL);
            continue;
        }
        if (recv_res.bytes <= 0) {
            break;
        }

        auto buf_slice = loop_.buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
        std::string_view chunk(reinterpret_cast<const char*>(buf_slice.data()), buf_slice.size());
        bool ok = co_await session_.process_incoming_data(chunk);
        loop_.buffer_pool().return_buffer(recv_res.bid);

        if (!ok || session_.is_closed()) {
            break;
        }
    }

    is_open_ = false;
    if (client_fd_ >= 0) {
        (void)(co_await loop_.ring().close(client_fd_));
        client_fd_ = -1;
    }
}

} // namespace aegon::http::websocket
