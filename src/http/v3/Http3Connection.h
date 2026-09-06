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
#include <span>
#include <cstdint>

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
    std::vector<std::pair<std::string, std::string>> header_storage;
    std::string body_accum;
    size_t body_offset{0};
    bool request_complete{false};
    bool response_submitted{false};
};

class Http3Connection {
public:
    Http3Connection(core::EventLoop& loop, int udp_fd, const sockaddr_storage& remote_addr,
                    socklen_t remote_addr_len, const Router& router, SSL_CTX* ssl_ctx,
                    void* user_state = nullptr);
    ~Http3Connection();

    Http3Connection(const Http3Connection&) = delete;
    Http3Connection& operator=(const Http3Connection&) = delete;

    /**
     * @brief Initialize QUIC and HTTP/3 sessions.
     */
    bool init(const uint8_t* dcid, size_t dcidlen, const uint8_t* scid, size_t scidlen);

    /**
     * @brief Process an incoming UDP datagram.
     */
    core::Task<bool> feed_datagram(std::span<const uint8_t> pkt);

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
    core::Task<void> dispatch_pending_requests();

    core::EventLoop& loop_;
    int udp_fd_;
    sockaddr_storage local_addr_{};
    socklen_t local_addr_len_{sizeof(sockaddr_storage)};
    sockaddr_storage remote_addr_{};
    socklen_t remote_addr_len_{sizeof(sockaddr_storage)};
    const Router& router_;
    SSL_CTX* ssl_ctx_{nullptr};
    void* user_state_{nullptr};

    SSL* ssl_{nullptr};
    ngtcp2_conn* qconn_{nullptr};
    nghttp3_conn* h3conn_{nullptr};
    ngtcp2_crypto_ossl_ctx* ossl_ctx_{nullptr};
    ngtcp2_crypto_conn_ref conn_ref_{};

    std::unordered_map<int64_t, std::unique_ptr<Http3Stream>> streams_;
    std::vector<int64_t> pending_dispatch_;
    std::vector<std::string> scids_;
    bool http3_streams_setup_{false};
    bool closed_{false};
};

} // namespace aegon::http::v3
