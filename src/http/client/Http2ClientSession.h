#pragma once

#include "http/Request.h"
#include "http/Response.h"
#include "http/client/Url.h"
#include "http/client/TlsClientStream.h"
#include "core/Task.h"
#include "core/EventLoop.h"

#include <nghttp2/nghttp2.h>
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
 * @brief Manages a native HTTP/2 client session (RFC 9113, RFC 7541) over TLS (h2)
 *        or cleartext prior-knowledge TCP (h2c).
 */
class Http2ClientSession {
public:
    explicit Http2ClientSession(const TlsClientOptions& tls_opts = {});
    ~Http2ClientSession();

    Http2ClientSession(const Http2ClientSession&) = delete;
    Http2ClientSession& operator=(const Http2ClientSession&) = delete;
    Http2ClientSession(Http2ClientSession&&) noexcept;
    Http2ClientSession& operator=(Http2ClientSession&&) noexcept;

    /**
     * @brief Connects to remote endpoint, performs HTTP/2 connection preface & SETTINGS,
     * submits the request, and yields the Response asynchronously.
     */
    core::Task<Response> execute(std::shared_ptr<RequestState> state, core::EventLoop& loop);

    /**
     * @brief Synchronous execution helper.
     */
    Response execute_sync(std::shared_ptr<RequestState> state);

    void close();

    [[nodiscard]] bool is_connected() const noexcept { return fd_ >= 0 && !closed_; }
    [[nodiscard]] bool is_tls() const noexcept { return is_tls_; }

    // Internal nghttp2 callbacks
    int on_header(const nghttp2_frame* frame, const uint8_t* name, size_t namelen,
                  const uint8_t* value, size_t valuelen, uint8_t flags);
    int on_data_chunk_recv(uint8_t flags, int32_t stream_id, const uint8_t* data, size_t len);
    int on_frame_recv(const nghttp2_frame* frame);
    int on_stream_close(int32_t stream_id, uint32_t error_code);
    ssize_t on_data_source_read(int32_t stream_id, uint8_t* buf, size_t length, uint32_t* data_flags);

private:
    bool connect_socket(const Url& url);
    bool init_tls(const Url& url);
    bool init_session();

    ssize_t write_wire_sync(const void* data, size_t len);
    ssize_t read_wire_sync(void* data, size_t len, int timeout_ms = 5000);

    core::Task<ssize_t> write_wire_async(const void* data, size_t len, core::EventLoop& loop);
    core::Task<ssize_t> read_wire_async(void* data, size_t len, core::EventLoop& loop);

    bool flush_outbound_sync();
    core::Task<bool> flush_outbound_async(core::EventLoop& loop);

    TlsClientOptions tls_opts_{};
    int fd_{-1};
    bool is_tls_{false};
    SSL_CTX* ssl_ctx_{nullptr};
    SSL* ssl_{nullptr};
    nghttp2_session* session_{nullptr};
    bool closed_{false};

    struct StreamContext {
        int32_t stream_id{-1};
        std::shared_ptr<RequestState> req_state;
        size_t body_offset{0};
        Response response{};
        bool complete{false};
    };

    std::unordered_map<int32_t, std::unique_ptr<StreamContext>> streams_;
    int32_t current_stream_id_{-1};
};

} // namespace aegon::http::client
