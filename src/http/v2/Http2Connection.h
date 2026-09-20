#pragma once

#include "http/Request.h"
#include "http/Response.h"
#include "http/Router.h"
#include "core/EventLoop.h"
#include <nghttp2/nghttp2.h>
#include <unordered_map>
#include <memory>
#include <string>
#include <string_view>
#include "http/ServiceRegistry.h"

namespace aegon::http::v2 {

struct Http2Stream {
    int32_t stream_id{-1};
    Request req;
    Response res;
    std::string path_storage;
    std::string query_storage;
    std::vector<std::pair<std::string, std::string>> header_storage;
    std::string body_accum;
    size_t body_offset{0};
    bool request_complete{false};
    bool response_submitted{false};
};

using OutputSender = std::function<core::Task<int>(std::span<const uint8_t>)>;

class Http2Connection {
public:
    Http2Connection(core::EventLoop& loop, int client_fd, const Router& router, 
                    const ServiceRegistry* services = nullptr, OutputSender sender = nullptr);
    ~Http2Connection();

    Http2Connection(const Http2Connection&) = delete;
    Http2Connection& operator=(const Http2Connection&) = delete;

    /**
     * @brief Initialize session, submit server SETTINGS frame, and flush initial handshake.
     */
    core::Task<bool> init();

    /**
     * @brief Handle RFC 9113 §3.2 HTTP/1.1 to HTTP/2 upgrade on stream 1.
     */
    core::Task<bool> upgrade_request(Request req, std::string_view http2_settings);

    /**
     * @brief Feed inbound wire data received from io_uring into nghttp2 state machine.
     */
    core::Task<bool> feed_data(const void* data, size_t len);

    /**
     * @brief Check if there are any completed requests and dispatch them through the router.
     */
    core::Task<void> dispatch_pending_requests();

    /**
     * @brief Flush all outbound queued frames from nghttp2 to client socket via io_uring.
     */
    core::Task<bool> flush_outbound();

    [[nodiscard]] bool wants_read() const noexcept;
    [[nodiscard]] bool wants_write() const noexcept;
    [[nodiscard]] bool is_closed() const noexcept;

    // nghttp2 callback trampolines
    int on_header(const nghttp2_frame* frame, const uint8_t* name, size_t namelen,
                  const uint8_t* value, size_t valuelen, uint8_t flags);
    int on_data_chunk_recv(uint8_t flags, int32_t stream_id, const uint8_t* data, size_t len);
    int on_frame_recv(const nghttp2_frame* frame);
    int on_stream_close(int32_t stream_id, uint32_t error_code);
    ssize_t on_data_source_read(int32_t stream_id, uint8_t* buf, size_t length, uint32_t* data_flags);

private:
    Http2Stream* get_or_create_stream(int32_t stream_id);
    void submit_response(Http2Stream* stream);

    core::EventLoop& loop_;
    int client_fd_;
    const Router& router_;
    const ServiceRegistry* services_{nullptr};

    nghttp2_session* session_{nullptr};
    std::unordered_map<int32_t, std::unique_ptr<Http2Stream>> streams_;
    std::vector<int32_t> pending_dispatch_;
    OutputSender sender_{nullptr};
    std::string outbound_buf_;
    bool closed_{false};
};

} // namespace aegon::http::v2
