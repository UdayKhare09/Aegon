#pragma once

#include "http/Request.h"
#include "http/Response.h"
#include "http/Router.h"
#include "core/Task.h"
#include "core/EventLoop.h"
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <nghttp3/nghttp3.h>
#include <openssl/ssl.h>
#include <netinet/in.h>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <span>
#include <cstdint>
#include <functional>

#include "http/ServiceRegistry.h"
#include "http/ServerConfig.h"

namespace aegon::http::v3 {

/**
 * @brief Checks if a header is prohibited in HTTP/3 per RFC 9114 §4.2.
 */
bool is_prohibited_header(std::string_view name);

struct Http3Stream {
    int64_t stream_id{-1};
    Request req;
    Response res;
    std::string path_storage;
    std::string query_storage;
    std::deque<std::pair<std::string, std::string>> header_storage;
    std::string body_accum;
    size_t body_offset{0};
    size_t headers_total_size{0};
    StatusCode error_status{StatusCode::Ok};
    bool request_complete{false};
    bool response_submitted{false};
    bool headers_received{false};
    bool seen_method{false};
    bool seen_path{false};
    bool seen_scheme{false};
    bool seen_authority{false};
    bool seen_regular_headers{false};

    // SSE streaming state
    std::string sse_buf;                // buffer of serialized frames
    size_t sse_offset{0};               // bytes transmitted so far
    bool is_sse{false};                 // true after ctx.sse() is called
    bool sse_eof{false};                // true after handler returns
};

class Http3Connection {
public:
    Http3Connection(core::EventLoop& loop, int udp_fd, const sockaddr_storage& remote_addr,
                    socklen_t remote_addr_len, const Router& router, SSL_CTX* ssl_ctx,
                    const ServiceRegistry* services = nullptr,
                    const ServerConfig& config = {});
    ~Http3Connection();

    Http3Connection(const Http3Connection&) = delete;
    Http3Connection& operator=(const Http3Connection&) = delete;

    /**
     * @brief Initialize QUIC and HTTP/3 sessions.
     */
    bool init(const uint8_t* dcid, size_t dcidlen, const uint8_t* scid, size_t scidlen, uint32_t version = NGTCP2_PROTO_VER_V1);

    /**
     * @brief Process an incoming UDP datagram.
     */
    core::Task<bool> feed_datagram(std::span<const uint8_t> pkt, const sockaddr_storage& from_addr, socklen_t from_len);

    /**
     * @brief Flush pending outbound QUIC packets over UDP.
     */
    bool flush_outbound();

    /**
     * @brief Return the nanosecond timestamp when the QUIC timer next expires (RFC 9002).
     */
    [[nodiscard]] uint64_t get_expiry() const noexcept;

    /**
     * @brief Handle timer expiry / loss detection tick (RFC 9002).
     */
    bool handle_expiry();

    /**
     * @brief Gracefully initiate HTTP/3 connection shutdown (RFC 9114 GOAWAY).
     */
    void shutdown();

    [[nodiscard]] bool is_closed() const noexcept;
    [[nodiscard]] const sockaddr_storage& remote_addr() const noexcept { return remote_addr_; }
    [[nodiscard]] ngtcp2_conn* conn() noexcept { return qconn_; }
    [[nodiscard]] nghttp3_conn* h3conn() noexcept { return h3conn_; }
    [[nodiscard]] const std::vector<std::string>& source_conn_ids() const noexcept { return scids_; }

    void setup_http3_streams();
    void add_source_conn_id(std::string_view cid);
    void remove_source_conn_id(std::string_view cid);

    using CidCallback = std::function<void(std::string_view)>;
    void set_cid_callbacks(CidCallback on_added, CidCallback on_removed) {
        on_cid_added_ = std::move(on_added);
        on_cid_removed_ = std::move(on_removed);
    }

    // nghttp3 callbacks
    int on_stream_header(int64_t stream_id, int32_t token, nghttp3_rcbuf* name, nghttp3_rcbuf* value, uint8_t flags);
    int on_stream_trailer(int64_t stream_id, int32_t token, nghttp3_rcbuf* name, nghttp3_rcbuf* value, uint8_t flags);
    int on_stream_end(int64_t stream_id);
    int on_stream_data(int64_t stream_id, const uint8_t* data, size_t datalen);
    int on_stream_close(int64_t stream_id, uint64_t app_error_code);
    nghttp3_ssize on_stream_read(int64_t stream_id, uint32_t* pflags, nghttp3_vec* vec, size_t veccnt);

private:
    Http3Stream* get_or_create_stream(int64_t stream_id);
    void submit_response(Http3Stream* stream);
    /**
     * @brief Submit HEADERS frame only (no body) for SSE streaming.
     */
    void submit_sse_headers(Http3Stream* stream);
    /**
     * @brief Push a pre-formatted SSE frame and flush QUIC packets.
     */
    void push_sse_frame(Http3Stream* stream, std::string frame);
    core::Task<void> dispatch_pending_requests();

    core::EventLoop& loop_;
    int udp_fd_;
    sockaddr_storage local_addr_{};
    socklen_t local_addr_len_{sizeof(sockaddr_storage)};
    sockaddr_storage remote_addr_{};
    socklen_t remote_addr_len_{sizeof(sockaddr_storage)};
    const Router& router_;
    SSL_CTX* ssl_ctx_{nullptr};
    const ServiceRegistry* services_{nullptr};

    SSL* ssl_{nullptr};
    ngtcp2_conn* qconn_{nullptr};
    nghttp3_conn* h3conn_{nullptr};
    ngtcp2_crypto_ossl_ctx* ossl_ctx_{nullptr};
    ngtcp2_crypto_conn_ref conn_ref_{};

    void on_stream_reset();

    std::unordered_map<int64_t, std::unique_ptr<Http3Stream>> streams_;
    std::vector<int64_t> pending_dispatch_;
    std::vector<std::string> scids_;
    bool http3_streams_setup_{false};
    bool closed_{false};
    uint32_t rst_count_{0};
    uint32_t rst_burst_limit_{100};
    int last_h3_error_{0};
    uint32_t version_{0};
    ngtcp2_cid client_dcid_{};
    ngtcp2_cid client_scid_{};
    CidCallback on_cid_added_{nullptr};
    CidCallback on_cid_removed_{nullptr};
    ServerConfig config_{};
};

} // namespace aegon::http::v3
