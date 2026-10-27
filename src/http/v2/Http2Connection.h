#pragma once

#include "http/Request.h"
#include "http/Response.h"
#include "http/Router.h"
#include "core/EventLoop.h"
#include "core/OsSocket.h"
#include <nghttp2/nghttp2.h>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>
#include <string_view>
#include <deque>
#include "http/ServiceRegistry.h"
#include "http/ServerConfig.h"

namespace aegon::http::v2 {

struct Http2Stream {
    int32_t stream_id{-1};
    Request req;
    Response res;
    std::string path_storage;
    std::string query_storage;
    static constexpr size_t INLINE_HEADER_ARENA_SIZE = 1024;
    char inline_header_arena[INLINE_HEADER_ARENA_SIZE];
    size_t inline_arena_used{0};
    std::deque<std::pair<std::string, std::string>> header_storage;

    void add_header(std::string_view n, std::string_view v) {
        if (inline_arena_used + n.size() + v.size() <= INLINE_HEADER_ARENA_SIZE) {
            char* n_dst = inline_header_arena + inline_arena_used;
            std::memcpy(n_dst, n.data(), n.size());
            inline_arena_used += n.size();

            char* v_dst = inline_header_arena + inline_arena_used;
            std::memcpy(v_dst, v.data(), v.size());
            inline_arena_used += v.size();

            req.headers().add({n_dst, n.size()}, {v_dst, v.size()});
        } else {
            header_storage.emplace_back(std::string(n), std::string(v));
            const auto& back = header_storage.back();
            req.headers().add(back.first, back.second);
        }
    }
    std::string body_accum;
    size_t body_offset{0};
    size_t headers_total_size{0};
    StatusCode error_status{StatusCode::Ok};
    bool request_complete{false};
    bool response_submitted{false};
    bool reset{false};
    bool trailers_submitted{false};

    // SSE streaming state
    std::deque<std::string> sse_frames;    // frames queued by SseWriteFn
    bool is_sse{false};                    // true after ctx.sse() is called
    bool sse_eof{false};                   // true after SseStream::close()

    // gRPC streaming state
    std::deque<std::string> grpc_frames;   // frames queued by GrpcSendFn
    bool is_grpc_stream{false};            // true after ctx.send() is called
    bool grpc_eof{false};                  // true when streaming handler completes
    std::shared_ptr<grpc::GrpcInboundChannel> grpc_inbound{nullptr};
    bool dispatched{false};
};

using OutputSender = std::function<core::Task<int>(std::span<const uint8_t>)>;

class Http2Connection {
public:
    Http2Connection(core::EventLoop& loop, core::os_socket_t client_fd, const Router& router, 
                    const ServiceRegistry* services = nullptr, OutputSender sender = nullptr,
                    const ServerConfig& config = {});
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

    void set_alt_svc(std::string alt_svc) noexcept { alt_svc_ = std::move(alt_svc); }

    [[nodiscard]] bool wants_read() const noexcept;
    [[nodiscard]] bool wants_write() const noexcept;
    [[nodiscard]] bool is_closed() const noexcept;

    // nghttp2 callback trampolines
    int on_header(const nghttp2_frame* frame, const uint8_t* name, size_t namelen,
                  const uint8_t* value, size_t valuelen, uint8_t flags);
    int on_data_chunk_recv(uint8_t flags, int32_t stream_id, const uint8_t* data, size_t len);
    int on_frame_recv(const nghttp2_frame* frame);
    int on_frame_send(const nghttp2_frame* frame);
    int on_invalid_frame_recv(const nghttp2_frame* frame, int lib_error_code);
    int on_stream_close(int32_t stream_id, uint32_t error_code);
    ssize_t on_data_source_read(int32_t stream_id, uint8_t* buf, size_t length, uint32_t* data_flags);

private:
    Http2Stream* get_or_create_stream(int32_t stream_id);
    void submit_response(Http2Stream* stream);
    void submit_trailers(Http2Stream* stream);
    /**
     * @brief Submit HEADERS frame only (no body) for SSE — must be called before dispatch.
     */
    void submit_sse_headers(Http2Stream* stream);
    void submit_grpc_headers(Http2Stream* stream);
    /**
     * @brief Push a pre-formatted SSE frame into the stream's outbound queue.
     * Resumes nghttp2 data sending and flushes to wire.
     */
    core::Task<void> push_sse_frame(Http2Stream* stream, std::string frame);
    /**
     * @brief Push a 5-byte framed gRPC response into the stream's outbound queue.
     */
    core::Task<void> push_grpc_frame(Http2Stream* stream, std::string frame);

    core::EventLoop& loop_;
    core::os_socket_t client_fd_;
    const Router& router_;
    const ServiceRegistry* services_{nullptr};

    nghttp2_session* session_{nullptr};
    std::unordered_map<int32_t, std::unique_ptr<Http2Stream>> streams_;
    std::vector<int32_t> pending_dispatch_;
    OutputSender sender_{nullptr};
    std::string outbound_buf_;
    std::string alt_svc_;
    bool closed_{false};
    uint32_t rst_count_{0};
    uint32_t rst_burst_limit_{100};
    int32_t max_remote_stream_id_{0};
    uint32_t last_error_code_{0};
    std::unordered_set<int32_t> closed_stream_ids_;
    ServerConfig config_{};
};

} // namespace aegon::http::v2
