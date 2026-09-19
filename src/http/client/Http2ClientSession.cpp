#include "http/client/Http2ClientSession.h"
#include "http/client/RequestBuilder.h"
#include "http/client/HttpClient.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <cctype>
#include <algorithm>

namespace aegon::http::client {

namespace {

inline nghttp2_nv make_nv(std::string_view name, std::string_view val) {
    return nghttp2_nv{
        .name = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(name.data())),
        .value = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(val.data())),
        .namelen = name.size(),
        .valuelen = val.size(),
        .flags = NGHTTP2_NV_FLAG_NONE
    };
}

int on_h2_header_cb(nghttp2_session*, const nghttp2_frame* frame,
                    const uint8_t* name, size_t namelen,
                    const uint8_t* value, size_t valuelen,
                    uint8_t flags, void* user_data) {
    return static_cast<Http2ClientSession*>(user_data)->on_header(
        frame, name, namelen, value, valuelen, flags);
}

int on_h2_data_chunk_recv_cb(nghttp2_session*, uint8_t flags,
                             int32_t stream_id, const uint8_t* data,
                             size_t len, void* user_data) {
    return static_cast<Http2ClientSession*>(user_data)->on_data_chunk_recv(
        flags, stream_id, data, len);
}

int on_h2_frame_recv_cb(nghttp2_session*, const nghttp2_frame* frame, void* user_data) {
    return static_cast<Http2ClientSession*>(user_data)->on_frame_recv(frame);
}

int on_h2_stream_close_cb(nghttp2_session*, int32_t stream_id, uint32_t error_code, void* user_data) {
    return static_cast<Http2ClientSession*>(user_data)->on_stream_close(stream_id, error_code);
}

ssize_t h2_client_data_source_read(nghttp2_session*, int32_t stream_id,
                                   uint8_t* buf, size_t length,
                                   uint32_t* data_flags,
                                   nghttp2_data_source*,
                                   void* user_data) {
    return static_cast<Http2ClientSession*>(user_data)->on_data_source_read(
        stream_id, buf, length, data_flags);
}

} // anonymous namespace

Http2ClientSession::Http2ClientSession(const TlsClientOptions& tls_opts)
    : tls_opts_(tls_opts) {}

Http2ClientSession::~Http2ClientSession() {
    close();
}

Http2ClientSession::Http2ClientSession(Http2ClientSession&& other) noexcept
    : tls_opts_(std::move(other.tls_opts_)), fd_(other.fd_), is_tls_(other.is_tls_),
      ssl_ctx_(other.ssl_ctx_), ssl_(other.ssl_), session_(other.session_),
      closed_(other.closed_), streams_(std::move(other.streams_)),
      current_stream_id_(other.current_stream_id_) {
    other.fd_ = -1;
    other.ssl_ = nullptr;
    other.ssl_ctx_ = nullptr;
    other.session_ = nullptr;
    other.closed_ = true;
}

Http2ClientSession& Http2ClientSession::operator=(Http2ClientSession&& other) noexcept {
    if (this != &other) {
        close();
        tls_opts_ = std::move(other.tls_opts_);
        fd_ = other.fd_;
        is_tls_ = other.is_tls_;
        ssl_ctx_ = other.ssl_ctx_;
        ssl_ = other.ssl_;
        session_ = other.session_;
        closed_ = other.closed_;
        streams_ = std::move(other.streams_);
        current_stream_id_ = other.current_stream_id_;

        other.fd_ = -1;
        other.ssl_ = nullptr;
        other.ssl_ctx_ = nullptr;
        other.session_ = nullptr;
        other.closed_ = true;
    }
    return *this;
}

void Http2ClientSession::close() {
    if (closed_) return;
    closed_ = true;

    if (session_) {
        nghttp2_session_del(session_);
        session_ = nullptr;
    }
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    streams_.clear();
}

bool Http2ClientSession::connect_socket(const Url& url) {
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* res = nullptr;
    std::string host_str(url.host());
    std::string port_str = std::to_string(url.port());

    int rc = ::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0 || !res) return false;

    fd_ = ::socket(res->ai_family, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (fd_ < 0) {
        ::freeaddrinfo(res);
        return false;
    }

    int one = 1;
    ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    if (::connect(fd_, res->ai_addr, res->ai_addrlen) != 0) {
        ::freeaddrinfo(res);
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    ::freeaddrinfo(res);
    return true;
}

bool Http2ClientSession::init_tls(const Url& url) {
    const SSL_METHOD* method = TLS_client_method();
    ssl_ctx_ = SSL_CTX_new(method);
    if (!ssl_ctx_) return false;

    SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_2_VERSION);
    SSL_CTX_set_options(ssl_ctx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);

    static const unsigned char H2_ALPN[] = "\x02h2\x08http/1.1";
    SSL_CTX_set_alpn_protos(ssl_ctx_, H2_ALPN, sizeof(H2_ALPN) - 1);

    if (tls_opts_.insecure_skip_verify) {
        SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_NONE, nullptr);
    } else {
        SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_default_verify_paths(ssl_ctx_);
        if (tls_opts_.ca_bundle_path.has_value() && !tls_opts_.ca_bundle_path->empty()) {
            SSL_CTX_load_verify_locations(ssl_ctx_, tls_opts_.ca_bundle_path->c_str(), nullptr);
        }
    }

    if (tls_opts_.client_cert_path && tls_opts_.client_key_path) {
        SSL_CTX_use_certificate_chain_file(ssl_ctx_, tls_opts_.client_cert_path->c_str());
        SSL_CTX_use_PrivateKey_file(ssl_ctx_, tls_opts_.client_key_path->c_str(), SSL_FILETYPE_PEM);
    }

    ssl_ = SSL_new(ssl_ctx_);
    if (!ssl_) return false;

    std::string host_str(url.host());
    if (!host_str.empty()) {
        SSL_set_tlsext_host_name(ssl_, host_str.c_str());
    }

    SSL_set_fd(ssl_, fd_);

    if (SSL_connect(ssl_) <= 0) {
        return false;
    }

    is_tls_ = true;
    return true;
}

bool Http2ClientSession::init_session() {
    nghttp2_session_callbacks* callbacks = nullptr;
    nghttp2_session_callbacks_new(&callbacks);

    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_h2_header_cb);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_h2_data_chunk_recv_cb);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_h2_frame_recv_cb);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_h2_stream_close_cb);

    int rv = nghttp2_session_client_new(&session_, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);
    if (rv != 0 || !session_) return false;

    nghttp2_settings_entry iv[] = {
        {NGHTTP2_SETTINGS_ENABLE_PUSH, 0},
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 1048576},
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}
    };
    rv = nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, iv, 3);
    return (rv == 0);
}

int Http2ClientSession::on_header(const nghttp2_frame* frame, const uint8_t* name, size_t namelen,
                                  const uint8_t* value, size_t valuelen, uint8_t) {
    if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_RESPONSE) {
        return 0;
    }

    auto it = streams_.find(frame->hd.stream_id);
    if (it == streams_.end()) return 0;

    std::string_view n(reinterpret_cast<const char*>(name), namelen);
    std::string_view v(reinterpret_cast<const char*>(value), valuelen);

    if (n == ":status") {
        uint16_t code = 0;
        for (char c : v) {
            if (c >= '0' && c <= '9') {
                code = code * 10 + (c - '0');
            }
        }
        it->second->response.status(code);
        it->second->response.version(HttpVersion::Http2);
    } else {
        it->second->response.set_header_owned(std::string(n), std::string(v));
    }
    return 0;
}

int Http2ClientSession::on_data_chunk_recv(uint8_t, int32_t stream_id, const uint8_t* data, size_t len) {
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) return 0;

    std::string b = std::string(it->second->response.body());
    b.append(reinterpret_cast<const char*>(data), len);
    it->second->response.body(std::move(b));
    return 0;
}

int Http2ClientSession::on_frame_recv(const nghttp2_frame*) {
    return 0;
}

int Http2ClientSession::on_stream_close(int32_t stream_id, uint32_t) {
    auto it = streams_.find(stream_id);
    if (it != streams_.end()) {
        it->second->complete = true;
    }
    return 0;
}

ssize_t Http2ClientSession::on_data_source_read(int32_t stream_id, uint8_t* buf, size_t length, uint32_t* data_flags) {
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) return NGHTTP2_ERR_CALLBACK_FAILURE;

    auto* st = it->second.get();
    const std::string& body = st->req_state->body;
    size_t available = (st->body_offset < body.size()) ? (body.size() - st->body_offset) : 0;
    size_t to_copy = std::min(length, available);

    if (to_copy > 0) {
        std::memcpy(buf, body.data() + st->body_offset, to_copy);
        st->body_offset += to_copy;
    }

    if (st->body_offset >= body.size()) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }
    return static_cast<ssize_t>(to_copy);
}

ssize_t Http2ClientSession::write_wire_sync(const void* data, size_t len) {
    if (closed_ || fd_ < 0) return -1;
    if (is_tls_) {
        return SSL_write(ssl_, data, static_cast<int>(len));
    }
    return ::send(fd_, data, len, 0);
}

ssize_t Http2ClientSession::read_wire_sync(void* data, size_t len, int timeout_ms) {
    if (closed_ || fd_ < 0) return -1;

    struct pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    int pr = ::poll(&pfd, 1, timeout_ms);
    if (pr <= 0 || !(pfd.revents & POLLIN)) {
        return pr == 0 ? 0 : -1;
    }

    if (is_tls_) {
        int n = SSL_read(ssl_, data, static_cast<int>(len));
        return n;
    }
    return ::recv(fd_, data, len, 0);
}

bool Http2ClientSession::flush_outbound_sync() {
    if (!session_ || closed_) return false;
    while (nghttp2_session_want_write(session_)) {
        const uint8_t* data = nullptr;
        ssize_t len = nghttp2_session_mem_send(session_, &data);
        if (len < 0) {
            closed_ = true;
            return false;
        }
        if (len == 0) break;

        size_t total_sent = 0;
        while (total_sent < static_cast<size_t>(len)) {
            ssize_t n = write_wire_sync(data + total_sent, len - total_sent);
            if (n <= 0) {
                closed_ = true;
                return false;
            }
            total_sent += static_cast<size_t>(n);
        }
    }
    return true;
}

core::Task<ssize_t> Http2ClientSession::write_wire_async(const void* data, size_t len, core::EventLoop& loop) {
    if (closed_ || fd_ < 0) co_return -1;
    if (is_tls_) {
        // Use synchronous SSL_write on non-blocking fd with loop yield if needed
        int n = SSL_write(ssl_, data, static_cast<int>(len));
        co_return n;
    }
    int n = co_await loop.ring().send(fd_, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data), len));
    co_return n;
}

core::Task<ssize_t> Http2ClientSession::read_wire_async(void* data, size_t len, core::EventLoop& loop) {
    if (closed_ || fd_ < 0) co_return -1;
    if (is_tls_) {
        int n = SSL_read(ssl_, data, static_cast<int>(len));
        co_return n;
    }
    int n = co_await loop.ring().recv(fd_, data, len, 0);
    co_return n;
}

core::Task<bool> Http2ClientSession::flush_outbound_async(core::EventLoop& loop) {
    if (!session_ || closed_) co_return false;
    while (nghttp2_session_want_write(session_)) {
        const uint8_t* data = nullptr;
        ssize_t len = nghttp2_session_mem_send(session_, &data);
        if (len < 0) {
            closed_ = true;
            co_return false;
        }
        if (len == 0) break;

        size_t total_sent = 0;
        while (total_sent < static_cast<size_t>(len)) {
            ssize_t n = co_await write_wire_async(data + total_sent, len - total_sent, loop);
            if (n <= 0) {
                closed_ = true;
                co_return false;
            }
            total_sent += static_cast<size_t>(n);
        }
    }
    co_return true;
}

Response Http2ClientSession::execute_sync(std::shared_ptr<RequestState> state) {
    auto url_opt = Url::parse(state->url);
    if (!url_opt) {
        Response bad_res;
        bad_res.status(StatusCode::BadRequest).body("Invalid URL for HTTP/2 request");
        return bad_res;
    }

    if (!connect_socket(*url_opt)) {
        Response err;
        err.status(StatusCode::BadGateway).body("Failed to connect socket to host");
        return err;
    }

    if (url_opt->is_https()) {
        if (!init_tls(*url_opt)) {
            Response err;
            err.status(StatusCode::BadGateway).body("Failed to complete TLS handshake for HTTP/2");
            return err;
        }
    }

    if (!init_session()) {
        Response err;
        err.status(StatusCode::BadGateway).body("Failed to initialize nghttp2 client session");
        return err;
    }

    auto stream_ctx = std::make_unique<StreamContext>();
    stream_ctx->req_state = state;
    auto* sctx = stream_ctx.get();

    std::string method_str = std::string(to_string(state->method));
    std::string scheme_str = is_tls_ ? "https" : "http";
    std::string authority_str = url_opt->host_header();
    std::string path_str = url_opt->target();
    std::string cl_str = std::to_string(state->body.size());

    std::vector<nghttp2_nv> nva;
    nva.push_back(make_nv(":method", method_str));
    nva.push_back(make_nv(":scheme", scheme_str));
    nva.push_back(make_nv(":authority", authority_str));
    nva.push_back(make_nv(":path", path_str));

    std::vector<std::string> lnames;
    for (const auto& h : state->headers) {
        std::string ln = h.name;
        for (char& c : ln) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ln == "host" || ln == "connection" || ln == "keep-alive" ||
            ln == "proxy-connection" || ln == "transfer-encoding" || ln == "upgrade") {
            continue;
        }
        lnames.push_back(std::move(ln));
        nva.push_back(make_nv(lnames.back(), h.value));
    }

    if (!state->cookies.empty()) {
        std::string cookie_str;
        for (size_t i = 0; i < state->cookies.size(); ++i) {
            if (i > 0) cookie_str += "; ";
            cookie_str += state->cookies[i];
        }
        lnames.push_back(std::move(cookie_str));
        nva.push_back(make_nv("cookie", lnames.back()));
    }

    if (!state->body.empty()) {
        nva.push_back(make_nv("content-length", cl_str));
    }

    nghttp2_data_provider prd;
    prd.source.ptr = sctx;
    prd.read_callback = h2_client_data_source_read;

    int32_t stream_id = nghttp2_submit_request(
        session_, nullptr, nva.data(), nva.size(),
        (!state->body.empty()) ? &prd : nullptr, sctx);

    if (stream_id < 0) {
        Response err;
        err.status(StatusCode::BadGateway).body("Failed to submit HTTP/2 request frame");
        return err;
    }

    sctx->stream_id = stream_id;
    current_stream_id_ = stream_id;
    streams_.emplace(stream_id, std::move(stream_ctx));

    flush_outbound_sync();

    uint8_t in_buf[16384];
    auto start_time = std::chrono::steady_clock::now();
    auto timeout = state->timeout;

    while (!sctx->complete && !closed_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        if (elapsed >= timeout) {
            Response err;
            err.status(StatusCode::GatewayTimeout).body("HTTP/2 request timed out");
            return err;
        }

        ssize_t n = read_wire_sync(in_buf, sizeof(in_buf), 500);
        if (n > 0) {
            ssize_t rv = nghttp2_session_mem_recv(session_, in_buf, static_cast<size_t>(n));
            if (rv < 0) {
                break;
            }
            flush_outbound_sync();
        } else if (n < 0) {
            break;
        }
    }

    auto it = streams_.find(current_stream_id_);
    if (it != streams_.end()) {
        return std::move(it->second->response);
    }
    Response err;
    err.status(StatusCode::BadGateway).body("HTTP/2 connection closed unexpectedly");
    return err;
}

core::Task<Response> Http2ClientSession::execute(std::shared_ptr<RequestState> state, core::EventLoop& loop) {
    auto url_opt = Url::parse(state->url);
    if (!url_opt) {
        Response bad_res;
        bad_res.status(StatusCode::BadRequest).body("Invalid URL for HTTP/2 request");
        co_return bad_res;
    }

    if (!connect_socket(*url_opt)) {
        Response err;
        err.status(StatusCode::BadGateway).body("Failed to connect socket to host");
        co_return err;
    }

    if (url_opt->is_https()) {
        if (!init_tls(*url_opt)) {
            Response err;
            err.status(StatusCode::BadGateway).body("Failed to complete TLS handshake for HTTP/2");
            co_return err;
        }
    }

    if (!init_session()) {
        Response err;
        err.status(StatusCode::BadGateway).body("Failed to initialize nghttp2 client session");
        co_return err;
    }

    auto stream_ctx = std::make_unique<StreamContext>();
    stream_ctx->req_state = state;
    auto* sctx = stream_ctx.get();

    std::string method_str = std::string(to_string(state->method));
    std::string scheme_str = is_tls_ ? "https" : "http";
    std::string authority_str = url_opt->host_header();
    std::string path_str = url_opt->target();
    std::string cl_str = std::to_string(state->body.size());

    std::vector<nghttp2_nv> nva;
    nva.push_back(make_nv(":method", method_str));
    nva.push_back(make_nv(":scheme", scheme_str));
    nva.push_back(make_nv(":authority", authority_str));
    nva.push_back(make_nv(":path", path_str));

    std::vector<std::string> lnames;
    for (const auto& h : state->headers) {
        std::string ln = h.name;
        for (char& c : ln) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ln == "host" || ln == "connection" || ln == "keep-alive" ||
            ln == "proxy-connection" || ln == "transfer-encoding" || ln == "upgrade") {
            continue;
        }
        lnames.push_back(std::move(ln));
        nva.push_back(make_nv(lnames.back(), h.value));
    }

    if (!state->cookies.empty()) {
        std::string cookie_str;
        for (size_t i = 0; i < state->cookies.size(); ++i) {
            if (i > 0) cookie_str += "; ";
            cookie_str += state->cookies[i];
        }
        lnames.push_back(std::move(cookie_str));
        nva.push_back(make_nv("cookie", lnames.back()));
    }

    if (!state->body.empty()) {
        nva.push_back(make_nv("content-length", cl_str));
    }

    nghttp2_data_provider prd;
    prd.source.ptr = sctx;
    prd.read_callback = h2_client_data_source_read;

    int32_t stream_id = nghttp2_submit_request(
        session_, nullptr, nva.data(), nva.size(),
        (!state->body.empty()) ? &prd : nullptr, sctx);

    if (stream_id < 0) {
        Response err;
        err.status(StatusCode::BadGateway).body("Failed to submit HTTP/2 request frame");
        co_return err;
    }

    sctx->stream_id = stream_id;
    current_stream_id_ = stream_id;
    streams_.emplace(stream_id, std::move(stream_ctx));

    co_await flush_outbound_async(loop);

    uint8_t in_buf[16384];

    while (!sctx->complete && !closed_) {
        ssize_t n = co_await read_wire_async(in_buf, sizeof(in_buf), loop);
        if (n > 0) {
            ssize_t rv = nghttp2_session_mem_recv(session_, in_buf, static_cast<size_t>(n));
            if (rv < 0) {
                break;
            }
            co_await flush_outbound_async(loop);
        } else if (n < 0) {
            break;
        }
    }

    auto it = streams_.find(current_stream_id_);
    if (it != streams_.end()) {
        co_return std::move(it->second->response);
    }
    Response err;
    err.status(StatusCode::BadGateway).body("HTTP/2 connection closed unexpectedly");
    co_return err;
}

} // namespace aegon::http::client
