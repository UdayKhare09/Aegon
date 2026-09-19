#include "http/client/Http3ClientSession.h"
#include "http/client/RequestBuilder.h"
#include "http/client/HttpClient.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <unistd.h>
#include <netdb.h>
#include <openssl/rand.h>

namespace aegon::http::client {

namespace {

uint64_t get_timestamp_ns() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

inline nghttp3_nv make_nv(std::string_view name, std::string_view val) {
    return nghttp3_nv{
        .name = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(name.data())),
        .value = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(val.data())),
        .namelen = name.size(),
        .valuelen = val.size(),
        .flags = NGHTTP3_NV_FLAG_NONE
    };
}

nghttp3_ssize h3_client_read_data(nghttp3_conn*, int64_t stream_id, nghttp3_vec* vec, size_t veccnt,
                                  uint32_t* pflags, void* user_data, void*) {
    if (veccnt == 0) return 0;
    auto* session = static_cast<Http3ClientSession*>(user_data);
    return session->on_stream_read(stream_id, pflags, vec, veccnt);
}

} // anonymous namespace

Http3ClientSession::Http3ClientSession(const TlsClientOptions& tls_opts)
    : tls_opts_(tls_opts) {}

Http3ClientSession::~Http3ClientSession() {
    close();
}

Http3ClientSession::Http3ClientSession(Http3ClientSession&& other) noexcept
    : tls_opts_(std::move(other.tls_opts_)), ssl_ctx_(other.ssl_ctx_), ssl_(other.ssl_),
      ossl_ctx_(other.ossl_ctx_), qconn_(other.qconn_), h3conn_(other.h3conn_),
      conn_ref_(other.conn_ref_), udp_fd_(other.udp_fd_),
      local_addr_(other.local_addr_), local_addr_len_(other.local_addr_len_),
      remote_addr_(other.remote_addr_), remote_addr_len_(other.remote_addr_len_),
      handshake_done_(other.handshake_done_), http3_streams_setup_(other.http3_streams_setup_),
      closed_(other.closed_), streams_(std::move(other.streams_)),
      current_stream_id_(other.current_stream_id_) {
    other.ssl_ctx_ = nullptr;
    other.ssl_ = nullptr;
    other.ossl_ctx_ = nullptr;
    other.qconn_ = nullptr;
    other.h3conn_ = nullptr;
    other.udp_fd_ = -1;
    other.closed_ = true;
}

Http3ClientSession& Http3ClientSession::operator=(Http3ClientSession&& other) noexcept {
    if (this != &other) {
        close();
        tls_opts_ = std::move(other.tls_opts_);
        ssl_ctx_ = other.ssl_ctx_;
        ssl_ = other.ssl_;
        ossl_ctx_ = other.ossl_ctx_;
        qconn_ = other.qconn_;
        h3conn_ = other.h3conn_;
        conn_ref_ = other.conn_ref_;
        udp_fd_ = other.udp_fd_;
        local_addr_ = other.local_addr_;
        local_addr_len_ = other.local_addr_len_;
        remote_addr_ = other.remote_addr_;
        remote_addr_len_ = other.remote_addr_len_;
        handshake_done_ = other.handshake_done_;
        http3_streams_setup_ = other.http3_streams_setup_;
        closed_ = other.closed_;
        streams_ = std::move(other.streams_);
        current_stream_id_ = other.current_stream_id_;

        other.ssl_ctx_ = nullptr;
        other.ssl_ = nullptr;
        other.ossl_ctx_ = nullptr;
        other.qconn_ = nullptr;
        other.h3conn_ = nullptr;
        other.udp_fd_ = -1;
        other.closed_ = true;
    }
    return *this;
}

void Http3ClientSession::close() {
    if (closed_) return;
    closed_ = true;

    if (h3conn_) {
        nghttp3_conn_del(h3conn_);
        h3conn_ = nullptr;
    }
    if (qconn_) {
        ngtcp2_conn_del(qconn_);
        qconn_ = nullptr;
    }
    if (ossl_ctx_) {
        ngtcp2_crypto_ossl_ctx_del(ossl_ctx_);
        ossl_ctx_ = nullptr;
    }
    if (ssl_) {
        SSL_set_app_data(ssl_, nullptr);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
    if (udp_fd_ >= 0) {
        ::close(udp_fd_);
        udp_fd_ = -1;
    }
    streams_.clear();
}

void Http3ClientSession::setup_http3_streams() {
    if (http3_streams_setup_ || !qconn_ || !h3conn_) return;

    int64_t ctrl_stream_id = -1;
    if (ngtcp2_conn_open_uni_stream(qconn_, &ctrl_stream_id, nullptr) != 0) {
        return;
    }
    nghttp3_conn_bind_control_stream(h3conn_, ctrl_stream_id);

    int64_t qenc_stream_id = -1;
    int64_t qdec_stream_id = -1;
    if (ngtcp2_conn_open_uni_stream(qconn_, &qenc_stream_id, nullptr) != 0) {
        return;
    }
    if (ngtcp2_conn_open_uni_stream(qconn_, &qdec_stream_id, nullptr) != 0) {
        return;
    }
    nghttp3_conn_bind_qpack_streams(h3conn_, qenc_stream_id, qdec_stream_id);

    http3_streams_setup_ = true;
    flush_outbound();
}

bool Http3ClientSession::init_quic(const Url& url) {
    // 1. Resolve Remote Address & Connect UDP Socket
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo* res = nullptr;
    std::string host_str(url.host());
    std::string port_str = std::to_string(url.port());

    int rc = ::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0 || !res) return false;

    udp_fd_ = ::socket(res->ai_family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
    if (udp_fd_ < 0) {
        ::freeaddrinfo(res);
        return false;
    }

    if (::connect(udp_fd_, res->ai_addr, res->ai_addrlen) != 0) {
        ::freeaddrinfo(res);
        ::close(udp_fd_);
        udp_fd_ = -1;
        return false;
    }

    std::memcpy(&remote_addr_, res->ai_addr, res->ai_addrlen);
    remote_addr_len_ = res->ai_addrlen;
    ::freeaddrinfo(res);

    local_addr_len_ = sizeof(local_addr_);
    ::getsockname(udp_fd_, reinterpret_cast<sockaddr*>(&local_addr_), &local_addr_len_);

    // 2. OpenSSL 3 TLS Context & Client Session Configuration
    const SSL_METHOD* method = TLS_client_method();
    ssl_ctx_ = SSL_CTX_new(method);
    if (!ssl_ctx_) return false;

    SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_3_VERSION);

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

    SSL_set_connect_state(ssl_);
    SSL_set_quic_tls_early_data_enabled(ssl_, 1);
    SSL_set_alpn_protos(ssl_, reinterpret_cast<const unsigned char*>("\x02h3"), 3);

    if (!host_str.empty()) {
        SSL_set_tlsext_host_name(ssl_, host_str.c_str());
    }

    conn_ref_.get_conn = [](ngtcp2_crypto_conn_ref* ref) -> ngtcp2_conn* {
        return static_cast<Http3ClientSession*>(ref->user_data)->conn();
    };
    conn_ref_.user_data = this;

    SSL_set_app_data(ssl_, &conn_ref_);
    if (ngtcp2_crypto_ossl_configure_client_session(ssl_) != 0) {
        return false;
    }

    // 3. ngtcp2 Callbacks
    ngtcp2_callbacks qcb{};
    qcb.client_initial = ngtcp2_crypto_client_initial_cb;
    qcb.recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb;
    qcb.encrypt = ngtcp2_crypto_encrypt_cb;
    qcb.decrypt = ngtcp2_crypto_decrypt_cb;
    qcb.hp_mask = ngtcp2_crypto_hp_mask_cb;
    qcb.recv_retry = ngtcp2_crypto_recv_retry_cb;
    qcb.update_key = ngtcp2_crypto_update_key_cb;
    qcb.delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
    qcb.delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
    qcb.get_path_challenge_data = ngtcp2_crypto_get_path_challenge_data_cb;
    qcb.version_negotiation = ngtcp2_crypto_version_negotiation_cb;

    qcb.rand = [](uint8_t* dest, size_t destlen, const ngtcp2_rand_ctx*) {
        RAND_bytes(dest, static_cast<int>(destlen));
    };

    qcb.get_new_connection_id = [](ngtcp2_conn*, ngtcp2_cid* cid, uint8_t* token, size_t cidlen, void*) -> int {
        RAND_bytes(cid->data, static_cast<int>(cidlen));
        cid->datalen = cidlen;
        RAND_bytes(token, NGTCP2_STATELESS_RESET_TOKENLEN);
        return 0;
    };

    qcb.remove_connection_id = [](ngtcp2_conn*, const ngtcp2_cid*, void*) -> int {
        return 0;
    };

    qcb.recv_stream_data = [](ngtcp2_conn* conn, uint32_t flags, int64_t stream_id,
                              uint64_t, const uint8_t* data, size_t datalen,
                              void* user_data, void*) -> int {
        auto* self = static_cast<Http3ClientSession*>(user_data);
        if (!self->h3conn()) return 0;
        int fin = (flags & NGTCP2_STREAM_DATA_FLAG_FIN) ? 1 : 0;
        nghttp3_ssize nconsumed = nghttp3_conn_read_stream(self->h3conn(), stream_id, data, datalen, fin);
        if (nconsumed < 0) {
            return NGTCP2_ERR_CALLBACK_FAILURE;
        }
        ngtcp2_conn_extend_max_stream_offset(conn, stream_id, nconsumed);
        ngtcp2_conn_extend_max_offset(conn, nconsumed);
        return 0;
    };

    qcb.acked_stream_data_offset = [](ngtcp2_conn*, int64_t stream_id,
                                      uint64_t, uint64_t datalen, void* user_data, void*) -> int {
        auto* self = static_cast<Http3ClientSession*>(user_data);
        if (self->h3conn()) {
            nghttp3_conn_add_ack_offset(self->h3conn(), stream_id, datalen);
        }
        return 0;
    };

    qcb.stream_close = [](ngtcp2_conn*, uint32_t, int64_t stream_id,
                          uint64_t app_error_code, void* user_data, void*) -> int {
        auto* self = static_cast<Http3ClientSession*>(user_data);
        if (self->h3conn()) {
            nghttp3_conn_close_stream(self->h3conn(), stream_id, app_error_code);
        }
        return 0;
    };

    qcb.stream_reset = [](ngtcp2_conn*, int64_t stream_id, uint64_t,
                          uint64_t app_error_code, void* user_data, void*) -> int {
        auto* self = static_cast<Http3ClientSession*>(user_data);
        if (self->h3conn()) {
            nghttp3_conn_close_stream(self->h3conn(), stream_id, app_error_code);
        }
        return 0;
    };

    qcb.stream_stop_sending = [](ngtcp2_conn*, int64_t stream_id,
                                uint64_t app_error_code, void* user_data, void*) -> int {
        auto* self = static_cast<Http3ClientSession*>(user_data);
        if (self->h3conn()) {
            nghttp3_conn_close_stream(self->h3conn(), stream_id, app_error_code);
        }
        return 0;
    };

    qcb.extend_max_local_streams_uni = [](ngtcp2_conn*, uint64_t, void* user_data) -> int {
        auto* self = static_cast<Http3ClientSession*>(user_data);
        self->setup_http3_streams();
        return 0;
    };

    qcb.handshake_completed = [](ngtcp2_conn*, void* user_data) -> int {
        auto* self = static_cast<Http3ClientSession*>(user_data);
        self->handshake_done_ = true;
        self->setup_http3_streams();
        return 0;
    };

    // 4. ngtcp2 Settings & Params
    ngtcp2_settings qsettings{};
    ngtcp2_settings_default(&qsettings);
    qsettings.initial_ts = get_timestamp_ns();

    ngtcp2_transport_params qparams{};
    ngtcp2_transport_params_default(&qparams);
    qparams.initial_max_streams_bidi = 100;
    qparams.initial_max_streams_uni = 100;
    qparams.initial_max_stream_data_bidi_local = 1048576;
    qparams.initial_max_stream_data_bidi_remote = 1048576;
    qparams.initial_max_stream_data_uni = 1048576;
    qparams.initial_max_data = 10485760;
    qparams.max_idle_timeout = 30 * NGTCP2_SECONDS;
    qparams.active_connection_id_limit = 8;

    uint8_t dcid_bytes[16], scid_bytes[16];
    RAND_bytes(dcid_bytes, sizeof(dcid_bytes));
    RAND_bytes(scid_bytes, sizeof(scid_bytes));
    ngtcp2_cid dcid, scid;
    ngtcp2_cid_init(&dcid, dcid_bytes, sizeof(dcid_bytes));
    ngtcp2_cid_init(&scid, scid_bytes, sizeof(scid_bytes));

    ngtcp2_path path{};
    path.local.addr = reinterpret_cast<sockaddr*>(&local_addr_);
    path.local.addrlen = local_addr_len_;
    path.remote.addr = reinterpret_cast<sockaddr*>(&remote_addr_);
    path.remote.addrlen = remote_addr_len_;

    int rv = ngtcp2_conn_client_new(&qconn_, &dcid, &scid, &path, NGTCP2_PROTO_VER_V1,
                                    &qcb, &qsettings, &qparams, nullptr, this);
    if (rv != 0) return false;

    rv = ngtcp2_crypto_ossl_ctx_new(&ossl_ctx_, ssl_);
    if (rv != 0) return false;
    ngtcp2_conn_set_tls_native_handle(qconn_, ossl_ctx_);

    // 5. nghttp3 Client Callbacks & Settings
    nghttp3_callbacks h3_cb{};
    h3_cb.recv_header = [](nghttp3_conn*, int64_t stream_id, int32_t token,
                           nghttp3_rcbuf* name, nghttp3_rcbuf* value, uint8_t flags,
                           void* user_data, void*) -> int {
        return static_cast<Http3ClientSession*>(user_data)->on_recv_header(stream_id, token, name, value, flags);
    };
    h3_cb.recv_data = [](nghttp3_conn*, int64_t stream_id, const uint8_t* data, size_t datalen,
                         void* user_data, void*) -> int {
        return static_cast<Http3ClientSession*>(user_data)->on_recv_data(stream_id, data, datalen);
    };
    h3_cb.end_stream = [](nghttp3_conn*, int64_t stream_id, void* user_data, void*) -> int {
        return static_cast<Http3ClientSession*>(user_data)->on_end_stream(stream_id);
    };
    h3_cb.stream_close = [](nghttp3_conn*, int64_t stream_id, uint64_t app_error_code,
                            void* user_data, void*) -> int {
        return static_cast<Http3ClientSession*>(user_data)->on_stream_close(stream_id, app_error_code);
    };
    h3_cb.stop_sending = [](nghttp3_conn*, int64_t stream_id, uint64_t app_error_code,
                            void* user_data, void*) -> int {
        auto* self = static_cast<Http3ClientSession*>(user_data);
        ngtcp2_conn_shutdown_stream_read(self->conn(), 0, stream_id, app_error_code);
        return 0;
    };
    h3_cb.reset_stream = [](nghttp3_conn*, int64_t stream_id, uint64_t app_error_code,
                            void* user_data, void*) -> int {
        auto* self = static_cast<Http3ClientSession*>(user_data);
        ngtcp2_conn_shutdown_stream_write(self->conn(), 0, stream_id, app_error_code);
        return 0;
    };

    nghttp3_settings h3_settings{};
    nghttp3_settings_default(&h3_settings);
    h3_settings.qpack_max_dtable_capacity = 4096;
    h3_settings.qpack_blocked_streams = 100;

    rv = nghttp3_conn_client_new(&h3conn_, &h3_cb, &h3_settings, nghttp3_mem_default(), this);
    return (rv == 0);
}

int Http3ClientSession::on_recv_header(int64_t stream_id, int32_t, nghttp3_rcbuf* name, nghttp3_rcbuf* value, uint8_t) {
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) return 0;

    auto nbuf = nghttp3_rcbuf_get_buf(name);
    auto vbuf = nghttp3_rcbuf_get_buf(value);

    std::string_view n(reinterpret_cast<const char*>(nbuf.base), nbuf.len);
    std::string_view v(reinterpret_cast<const char*>(vbuf.base), vbuf.len);

    if (n == ":status") {
        uint16_t code = 0;
        for (char c : v) {
            if (c >= '0' && c <= '9') {
                code = code * 10 + (c - '0');
            }
        }
        it->second->response.status(code);
        it->second->response.version(HttpVersion::Http3);
    } else {
        it->second->response.set_header_owned(std::string(n), std::string(v));
    }
    return 0;
}

int Http3ClientSession::on_recv_data(int64_t stream_id, const uint8_t* data, size_t datalen) {
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) return 0;

    std::string b = std::string(it->second->response.body());
    b.append(reinterpret_cast<const char*>(data), datalen);
    it->second->response.body(std::move(b));
    return 0;
}

int Http3ClientSession::on_end_stream(int64_t stream_id) {
    auto it = streams_.find(stream_id);
    if (it != streams_.end()) {
        it->second->complete = true;
    }
    return 0;
}

int Http3ClientSession::on_stream_close(int64_t stream_id, uint64_t) {
    auto it = streams_.find(stream_id);
    if (it != streams_.end()) {
        it->second->complete = true;
    }
    return 0;
}

nghttp3_ssize Http3ClientSession::on_stream_read(int64_t stream_id, uint32_t* pflags, nghttp3_vec* vec, size_t veccnt) {
    if (veccnt == 0) return 0;
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) return NGHTTP3_ERR_CALLBACK_FAILURE;

    auto* st = it->second.get();
    const std::string& body = st->req_state->body;
    size_t available = (st->body_offset < body.size()) ? (body.size() - st->body_offset) : 0;

    vec[0].base = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(body.data() + st->body_offset));
    vec[0].len = available;
    st->body_offset += available;

    if (st->body_offset >= body.size()) {
        *pflags |= NGHTTP3_DATA_FLAG_EOF;
    }
    return 1;
}

bool Http3ClientSession::flush_outbound() {
    if (!qconn_ || closed_ || udp_fd_ < 0) return false;

    alignas(64) uint8_t out[65536];
    size_t max_payload = ngtcp2_conn_get_max_tx_udp_payload_size(qconn_);
    if (max_payload == 0 || max_payload > sizeof(out)) {
        max_payload = 1452;
    }

    ngtcp2_path_storage ps;
    ngtcp2_path_storage_init(&ps, reinterpret_cast<const ngtcp2_sockaddr*>(&local_addr_), local_addr_len_,
                             reinterpret_cast<const ngtcp2_sockaddr*>(&remote_addr_), remote_addr_len_, nullptr);

    while (true) {
        int64_t stream_id = -1;
        int fin = 0;
        nghttp3_vec vec[16];
        nghttp3_ssize veccnt = 0;

        if (h3conn_ && http3_streams_setup_) {
            veccnt = nghttp3_conn_writev_stream(h3conn_, &stream_id, &fin, vec, 16);
            if (veccnt < 0) {
                closed_ = true;
                return false;
            }
        }

        ngtcp2_ssize pdatalen = 0;
        uint32_t flags = (fin ? NGTCP2_WRITE_STREAM_FLAG_FIN : 0);
        ngtcp2_pkt_info pi{};
        ngtcp2_ssize nwrite = 0;

        if (stream_id >= 0) {
            nwrite = ngtcp2_conn_writev_stream(
                qconn_, &ps.path, &pi, out, max_payload, &pdatalen,
                flags, stream_id, reinterpret_cast<const ngtcp2_vec*>(vec), static_cast<size_t>(veccnt), get_timestamp_ns());
        } else {
            nwrite = ngtcp2_conn_write_pkt(qconn_, &ps.path, &pi, out, max_payload, get_timestamp_ns());
        }

        if (nwrite <= 0) {
            break;
        }

        if (pdatalen > 0) {
            nghttp3_conn_add_write_offset(h3conn_, stream_id, pdatalen);
        }

        ::send(udp_fd_, out, nwrite, 0);
    }
    return true;
}

bool Http3ClientSession::process_datagram(const uint8_t* data, size_t datalen) {
    if (!qconn_ || closed_) return false;

    ngtcp2_path path{};
    path.local.addr = reinterpret_cast<sockaddr*>(&local_addr_);
    path.local.addrlen = local_addr_len_;
    path.remote.addr = reinterpret_cast<sockaddr*>(&remote_addr_);
    path.remote.addrlen = remote_addr_len_;

    ngtcp2_pkt_info pi{};
    int rv = ngtcp2_conn_read_pkt(qconn_, &path, &pi, data, datalen, get_timestamp_ns());
    if (rv != 0) {
        return false;
    }
    return true;
}

Response Http3ClientSession::execute_sync(std::shared_ptr<RequestState> state) {
    auto url_opt = Url::parse(state->url);
    if (!url_opt) {
        Response bad_res;
        bad_res.status(StatusCode::BadRequest).body("Invalid URL for HTTP/3 request");
        return bad_res;
    }

    if (!init_quic(*url_opt)) {
        Response err;
        err.status(StatusCode::BadGateway).body("Failed to initialize QUIC/UDP socket");
        return err;
    }

    // Flush Initial QUIC packet to start handshake
    flush_outbound();

    bool request_submitted = false;
    auto stream_ctx = std::make_unique<StreamContext>();
    stream_ctx->req_state = state;
    auto* sctx = stream_ctx.get();

    uint8_t pkt_buf[65536];
    auto start_time = std::chrono::steady_clock::now();
    auto timeout = state->timeout;

    while (!sctx->complete && !closed_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        if (elapsed >= timeout) {
            Response err;
            err.status(StatusCode::GatewayTimeout).body("HTTP/3 request timed out");
            return err;
        }

        int poll_timeout_ms = 50;
        uint64_t expiry = ngtcp2_conn_get_expiry(qconn_);
        uint64_t now = get_timestamp_ns();
        if (expiry <= now) {
            ngtcp2_conn_handle_expiry(qconn_, now);
            flush_outbound();
            poll_timeout_ms = 1;
        } else if (expiry != UINT64_MAX) {
            uint64_t diff_ms = (expiry - now) / 1'000'000ULL;
            if (diff_ms < static_cast<uint64_t>(poll_timeout_ms)) {
                poll_timeout_ms = static_cast<int>(diff_ms);
                if (poll_timeout_ms <= 0) poll_timeout_ms = 1;
            }
        }

        struct pollfd pfd{};
        pfd.fd = udp_fd_;
        pfd.events = POLLIN;

        int pr = ::poll(&pfd, 1, poll_timeout_ms);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            while (true) {
                ssize_t n = ::recv(udp_fd_, pkt_buf, sizeof(pkt_buf), 0);
                if (n <= 0) break;
                process_datagram(pkt_buf, static_cast<size_t>(n));
            }
            flush_outbound();
        }

        // Once handshake is established and HTTP/3 streams are configured, submit HTTP/3 request
        if (handshake_done_ && !http3_streams_setup_) {
            setup_http3_streams();
        }

        if (http3_streams_setup_ && !request_submitted) {
            int64_t stream_id = -1;
            int rv = ngtcp2_conn_open_bidi_stream(qconn_, &stream_id, nullptr);
            if (rv == 0) {
                sctx->stream_id = stream_id;
                current_stream_id_ = stream_id;
                streams_.emplace(stream_id, std::move(stream_ctx));

                std::string method_str = std::string(to_string(state->method));
                std::string scheme_str = url_opt->is_https() ? "https" : "http";
                std::string authority_str = url_opt->host_header();
                std::string path_str = url_opt->target();
                std::string cl_str = std::to_string(state->body.size());

                std::vector<nghttp3_nv> nva;
                nva.push_back(make_nv(":method", method_str));
                nva.push_back(make_nv(":scheme", scheme_str));
                nva.push_back(make_nv(":authority", authority_str));
                nva.push_back(make_nv(":path", path_str));

                for (const auto& h : state->headers) {
                    if (iequals(h.name, "host") || iequals(h.name, "connection")) continue;
                    nva.push_back(make_nv(h.name, h.value));
                }

                if (!state->cookies.empty()) {
                    std::string cookie_str;
                    for (size_t i = 0; i < state->cookies.size(); ++i) {
                        if (i > 0) cookie_str += "; ";
                        cookie_str += state->cookies[i];
                    }
                    nva.push_back(make_nv("cookie", cookie_str));
                }

                if (!state->body.empty()) {
                    nva.push_back(make_nv("content-length", cl_str));
                }

                nghttp3_data_reader dr{};
                dr.read_data = h3_client_read_data;

                nghttp3_conn_submit_request(
                    h3conn_, stream_id, nva.data(), nva.size(),
                    (!state->body.empty()) ? &dr : nullptr, this);

                request_submitted = true;
                flush_outbound();
            }
        }
    }

    auto it = streams_.find(current_stream_id_);
    if (it != streams_.end()) {
        return std::move(it->second->response);
    }
    Response err;
    err.status(StatusCode::BadGateway).body("HTTP/3 connection closed before response completed");
    return err;
}

core::Task<Response> Http3ClientSession::execute(std::shared_ptr<RequestState> state, core::EventLoop& loop) {
    auto url_opt = Url::parse(state->url);
    if (!url_opt) {
        Response bad_res;
        bad_res.status(StatusCode::BadRequest).body("Invalid URL for HTTP/3 request");
        co_return bad_res;
    }

    if (!init_quic(*url_opt)) {
        Response err;
        err.status(StatusCode::BadGateway).body("Failed to initialize QUIC/UDP socket");
        co_return err;
    }

    flush_outbound();

    bool request_submitted = false;
    auto stream_ctx = std::make_unique<StreamContext>();
    stream_ctx->req_state = state;
    auto* sctx = stream_ctx.get();

    uint8_t pkt_buf[65536];

    while (!sctx->complete && !closed_) {
        uint64_t expiry = ngtcp2_conn_get_expiry(qconn_);
        uint64_t now = get_timestamp_ns();
        if (expiry <= now) {
            ngtcp2_conn_handle_expiry(qconn_, now);
            flush_outbound();
        }

        int n = co_await loop.ring().recv(udp_fd_, pkt_buf, sizeof(pkt_buf), 0);
        if (n > 0) {
            process_datagram(pkt_buf, static_cast<size_t>(n));
            while (true) {
                ssize_t n2 = ::recv(udp_fd_, pkt_buf, sizeof(pkt_buf), MSG_DONTWAIT);
                if (n2 <= 0) break;
                process_datagram(pkt_buf, static_cast<size_t>(n2));
            }
            flush_outbound();
        } else if (n < 0 && n != -EAGAIN && n != -EWOULDBLOCK) {
            break;
        }

        if (handshake_done_ && !http3_streams_setup_) {
            setup_http3_streams();
        }

        if (http3_streams_setup_ && !request_submitted) {
            int64_t stream_id = -1;
            int rv = ngtcp2_conn_open_bidi_stream(qconn_, &stream_id, nullptr);
            if (rv == 0) {
                sctx->stream_id = stream_id;
                current_stream_id_ = stream_id;
                streams_.emplace(stream_id, std::move(stream_ctx));

                std::string method_str = std::string(to_string(state->method));
                std::string scheme_str = url_opt->is_https() ? "https" : "http";
                std::string authority_str = url_opt->host_header();
                std::string path_str = url_opt->target();
                std::string cl_str = std::to_string(state->body.size());

                std::vector<nghttp3_nv> nva;
                nva.push_back(make_nv(":method", method_str));
                nva.push_back(make_nv(":scheme", scheme_str));
                nva.push_back(make_nv(":authority", authority_str));
                nva.push_back(make_nv(":path", path_str));

                for (const auto& h : state->headers) {
                    if (iequals(h.name, "host") || iequals(h.name, "connection")) continue;
                    nva.push_back(make_nv(h.name, h.value));
                }

                if (!state->cookies.empty()) {
                    std::string cookie_str;
                    for (size_t i = 0; i < state->cookies.size(); ++i) {
                        if (i > 0) cookie_str += "; ";
                        cookie_str += state->cookies[i];
                    }
                    nva.push_back(make_nv("cookie", cookie_str));
                }

                if (!state->body.empty()) {
                    nva.push_back(make_nv("content-length", cl_str));
                }

                nghttp3_data_reader dr{};
                dr.read_data = h3_client_read_data;

                nghttp3_conn_submit_request(
                    h3conn_, stream_id, nva.data(), nva.size(),
                    (!state->body.empty()) ? &dr : nullptr, this);

                request_submitted = true;
                flush_outbound();
            }
        }
    }

    auto it = streams_.find(current_stream_id_);
    if (it != streams_.end()) {
        co_return std::move(it->second->response);
    }
    Response err;
    err.status(StatusCode::BadGateway).body("HTTP/3 connection closed before response completed");
    co_return err;
}

} // namespace aegon::http::client
