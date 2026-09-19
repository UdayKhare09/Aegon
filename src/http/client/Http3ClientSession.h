#pragma once

#include "http/Request.h"
#include "http/Response.h"
#include "http/client/Url.h"
#include "http/client/TlsClientStream.h"
#include "core/Task.h"
#include "core/EventLoop.h"

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <nghttp3/nghttp3.h>
#include <openssl/ssl.h>
#include <netinet/in.h>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace aegon::http::client {

struct RequestState;

/**
 * @brief Manages a native HTTP/3 client session over QUIC (RFC 9000, RFC 9114, RFC 9204).
 */
class Http3ClientSession {
public:
    explicit Http3ClientSession(const TlsClientOptions& tls_opts = {});
    ~Http3ClientSession();

    Http3ClientSession(const Http3ClientSession&) = delete;
    Http3ClientSession& operator=(const Http3ClientSession&) = delete;
    Http3ClientSession(Http3ClientSession&&) noexcept;
    Http3ClientSession& operator=(Http3ClientSession&&) noexcept;

    /**
     * @brief Connects to remote endpoint via UDP, completes QUIC handshake,
     * submits the request, and yields the Response asynchronously.
     */
    core::Task<Response> execute(std::shared_ptr<RequestState> state, core::EventLoop& loop);

    /**
     * @brief Synchronous execution helper.
     */
    Response execute_sync(std::shared_ptr<RequestState> state);

    void close();

    [[nodiscard]] bool is_connected() const noexcept { return handshake_done_ && !closed_; }

    // Internal nghttp3 callbacks
    int on_recv_header(int64_t stream_id, int32_t token, nghttp3_rcbuf* name, nghttp3_rcbuf* value, uint8_t flags);
    int on_recv_data(int64_t stream_id, const uint8_t* data, size_t datalen);
    int on_end_stream(int64_t stream_id);
    int on_stream_close(int64_t stream_id, uint64_t app_error_code);
    nghttp3_ssize on_stream_read(int64_t stream_id, uint32_t* pflags, nghttp3_vec* vec, size_t veccnt);

    [[nodiscard]] ngtcp2_conn* conn() noexcept { return qconn_; }
    [[nodiscard]] nghttp3_conn* h3conn() noexcept { return h3conn_; }
    void setup_http3_streams();

private:
    bool init_quic(const Url& url);
    bool flush_outbound();
    bool process_datagram(const uint8_t* data, size_t datalen);

    TlsClientOptions tls_opts_{};
    SSL_CTX* ssl_ctx_{nullptr};
    SSL* ssl_{nullptr};
    ngtcp2_crypto_ossl_ctx* ossl_ctx_{nullptr};
    ngtcp2_conn* qconn_{nullptr};
    nghttp3_conn* h3conn_{nullptr};
    ngtcp2_crypto_conn_ref conn_ref_{};

    int udp_fd_{-1};
    sockaddr_storage local_addr_{};
    socklen_t local_addr_len_{0};
    sockaddr_storage remote_addr_{};
    socklen_t remote_addr_len_{0};

    bool handshake_done_{false};
    bool http3_streams_setup_{false};
    bool closed_{false};

    struct StreamContext {
        int64_t stream_id{-1};
        std::shared_ptr<RequestState> req_state;
        size_t body_offset{0};
        Response response{};
        bool complete{false};
    };

    std::unordered_map<int64_t, std::unique_ptr<StreamContext>> streams_;
    int64_t current_stream_id_{-1};
};

} // namespace aegon::http::client
