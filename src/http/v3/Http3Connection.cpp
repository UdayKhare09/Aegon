#include "http/v3/Http3Connection.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <cstdarg>
#include <openssl/rand.h>

namespace aegon::http::v3 {

namespace {

uint64_t get_timestamp_ns() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

// nghttp3 callbacks
int h3_recv_header(nghttp3_conn*, int64_t stream_id, int32_t token,
                   nghttp3_rcbuf* name, nghttp3_rcbuf* value, uint8_t flags,
                   void* user_data, void*) {
    return static_cast<Http3Connection*>(user_data)->on_stream_header(
        stream_id, token, name, value, flags);
}

int h3_end_stream(nghttp3_conn*, int64_t stream_id, void* user_data, void*) {
    return static_cast<Http3Connection*>(user_data)->on_stream_end(stream_id);
}

int h3_recv_data(nghttp3_conn*, int64_t stream_id, const uint8_t* data, size_t datalen,
                 void* user_data, void*) {
    return static_cast<Http3Connection*>(user_data)->on_stream_data(stream_id, data, datalen);
}

int h3_stream_close(nghttp3_conn*, int64_t stream_id, uint64_t app_error_code,
                    void* user_data, void*) {
    return static_cast<Http3Connection*>(user_data)->on_stream_close(stream_id, app_error_code);
}

nghttp3_ssize h3_read_data(nghttp3_conn*, int64_t stream_id, nghttp3_vec* vec, size_t veccnt,
                           uint32_t* pflags, void* user_data, void*) {
    return static_cast<Http3Connection*>(user_data)->on_stream_read(stream_id, pflags, vec, veccnt);
}

} // anonymous namespace

Http3Connection::Http3Connection(core::EventLoop& loop, int udp_fd, const sockaddr_storage& remote_addr,
                                 socklen_t remote_addr_len, const Router& router, SSL_CTX* ssl_ctx,
                                 void* user_state)
    : loop_(loop), udp_fd_(udp_fd), remote_addr_(remote_addr), remote_addr_len_(remote_addr_len),
      router_(router), ssl_ctx_(ssl_ctx), user_state_(user_state) {}

Http3Connection::~Http3Connection() {
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
}

void Http3Connection::add_source_conn_id(std::string_view cid) {
    scids_.emplace_back(cid);
}

void Http3Connection::setup_http3_streams() {
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
}

bool Http3Connection::init(const uint8_t* dcid, size_t dcidlen, const uint8_t* scid, size_t scidlen) {
    if (!ssl_ctx_) return false;

    ssl_ = SSL_new(ssl_ctx_);
    if (!ssl_) return false;

    SSL_set_accept_state(ssl_);
    SSL_set_quic_tls_early_data_enabled(ssl_, 1);

    conn_ref_.get_conn = [](ngtcp2_crypto_conn_ref* ref) -> ngtcp2_conn* {
        return static_cast<Http3Connection*>(ref->user_data)->conn();
    };
    conn_ref_.user_data = this;

    SSL_set_app_data(ssl_, &conn_ref_);
    if (ngtcp2_crypto_ossl_configure_server_session(ssl_) != 0) {
        return false;
    }

    // Initialize nghttp3 callbacks
    nghttp3_callbacks h3_cb{};
    h3_cb.recv_header = h3_recv_header;
    h3_cb.end_stream = h3_end_stream;
    h3_cb.recv_data = h3_recv_data;
    h3_cb.stream_close = h3_stream_close;

    nghttp3_settings h3_settings{};
    nghttp3_settings_default(&h3_settings);
    h3_settings.qpack_max_dtable_capacity = 4096;
    h3_settings.qpack_blocked_streams = 100;

    int rv = nghttp3_conn_server_new(&h3conn_, &h3_cb, &h3_settings, nghttp3_mem_default(), this);
    if (rv != 0) return false;

    // Initialize ngtcp2 callbacks
    ngtcp2_callbacks qcb{};
    qcb.recv_client_initial = ngtcp2_crypto_recv_client_initial_cb;
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

    qcb.get_new_connection_id = [](ngtcp2_conn*, ngtcp2_cid* cid, uint8_t* token, size_t cidlen, void* user_data) -> int {
        auto* self = static_cast<Http3Connection*>(user_data);
        RAND_bytes(cid->data, static_cast<int>(cidlen));
        cid->datalen = cidlen;
        RAND_bytes(token, NGTCP2_STATELESS_RESET_TOKENLEN);
        self->add_source_conn_id(std::string_view(reinterpret_cast<const char*>(cid->data), cid->datalen));
        return 0;
    };

    qcb.remove_connection_id = [](ngtcp2_conn*, const ngtcp2_cid*, void*) -> int {
        return 0;
    };

    qcb.recv_stream_data = [](ngtcp2_conn* conn, uint32_t flags, int64_t stream_id,
                              uint64_t, const uint8_t* data, size_t datalen,
                              void* user_data, void*) -> int {
        auto* self = static_cast<Http3Connection*>(user_data);
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
        auto* self = static_cast<Http3Connection*>(user_data);
        if (self->h3conn()) {
            nghttp3_conn_add_ack_offset(self->h3conn(), stream_id, datalen);
        }
        return 0;
    };

    qcb.stream_close = [](ngtcp2_conn*, uint32_t, int64_t stream_id,
                          uint64_t app_error_code, void* user_data, void*) -> int {
        auto* self = static_cast<Http3Connection*>(user_data);
        if (self->h3conn()) {
            nghttp3_conn_close_stream(self->h3conn(), stream_id, app_error_code);
        }
        return 0;
    };

    qcb.stream_reset = [](ngtcp2_conn*, int64_t stream_id, uint64_t,
                          uint64_t app_error_code, void* user_data, void*) -> int {
        auto* self = static_cast<Http3Connection*>(user_data);
        if (self->h3conn()) {
            nghttp3_conn_close_stream(self->h3conn(), stream_id, app_error_code);
        }
        return 0;
    };

    qcb.stream_stop_sending = [](ngtcp2_conn*, int64_t stream_id,
                                uint64_t app_error_code, void* user_data, void*) -> int {
        auto* self = static_cast<Http3Connection*>(user_data);
        if (self->h3conn()) {
            nghttp3_conn_close_stream(self->h3conn(), stream_id, app_error_code);
        }
        return 0;
    };

    qcb.extend_max_local_streams_uni = [](ngtcp2_conn*, uint64_t, void* user_data) -> int {
        auto* self = static_cast<Http3Connection*>(user_data);
        self->setup_http3_streams();
        return 0;
    };

    qcb.handshake_completed = [](ngtcp2_conn*, void* user_data) -> int {
        auto* self = static_cast<Http3Connection*>(user_data);
        self->setup_http3_streams();
        return 0;
    };

    ngtcp2_settings qsettings{};
    ngtcp2_settings_default(&qsettings);
    qsettings.initial_ts = get_timestamp_ns();
    qsettings.log_printf = nullptr;

    ngtcp2_transport_params qparams{};
    ngtcp2_transport_params_default(&qparams);
    qparams.initial_max_streams_bidi = 100;
    qparams.initial_max_streams_uni = 100;
    qparams.initial_max_stream_data_bidi_remote = 1048576;
    qparams.initial_max_stream_data_bidi_local = 1048576;
    qparams.initial_max_data = 10485760;

    // RFC 9000: Set original_dcid to the DCID from client's Initial packet
    qparams.original_dcid_present = 1;
    ngtcp2_cid_init(&qparams.original_dcid, dcid, dcidlen);

    // Generate local server SCID
    uint8_t local_scid[16];
    RAND_bytes(local_scid, sizeof(local_scid));
    scids_.emplace_back(reinterpret_cast<const char*>(local_scid), sizeof(local_scid));

    ngtcp2_cid qdcid, qscid;
    ngtcp2_cid_init(&qdcid, scid, scidlen); // Remote peer's SCID
    ngtcp2_cid_init(&qscid, local_scid, sizeof(local_scid)); // Local server's SCID

    getsockname(udp_fd_, reinterpret_cast<sockaddr*>(&local_addr_), &local_addr_len_);

    ngtcp2_path path{};
    path.local.addr = const_cast<sockaddr*>(reinterpret_cast<const sockaddr*>(&local_addr_));
    path.local.addrlen = local_addr_len_;
    path.remote.addr = const_cast<sockaddr*>(reinterpret_cast<const sockaddr*>(&remote_addr_));
    path.remote.addrlen = remote_addr_len_;

    rv = ngtcp2_conn_server_new(&qconn_, &qdcid, &qscid, &path, NGTCP2_PROTO_VER_V1,
                                &qcb, &qsettings, &qparams, nullptr, this);
    if (rv != 0) return false;

    rv = ngtcp2_crypto_ossl_ctx_new(&ossl_ctx_, ssl_);
    if (rv != 0) return false;

    ngtcp2_conn_set_tls_native_handle(qconn_, ossl_ctx_);
    return true;
}

Http3Stream* Http3Connection::get_or_create_stream(int64_t stream_id) {
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        auto stream = std::make_unique<Http3Stream>();
        stream->stream_id = stream_id;
        stream->req.set_version(HttpVersion::Http3);
        auto* ptr = stream.get();
        streams_.emplace(stream_id, std::move(stream));
        return ptr;
    }
    return it->second.get();
}

int Http3Connection::on_stream_header(int64_t stream_id, int32_t, nghttp3_rcbuf* name, nghttp3_rcbuf* value, uint8_t) {
    auto* stream = get_or_create_stream(stream_id);

    auto name_buf = nghttp3_rcbuf_get_buf(name);
    auto val_buf = nghttp3_rcbuf_get_buf(value);

    std::string_view n(reinterpret_cast<const char*>(name_buf.base), name_buf.len);
    std::string_view v(reinterpret_cast<const char*>(val_buf.base), val_buf.len);

    if (n == ":method") {
        stream->req.set_method(string_to_method(v));
    } else if (n == ":path") {
        size_t qmark = v.find('?');
        if (qmark != std::string_view::npos) {
            stream->path_storage.assign(v.data(), qmark);
            stream->query_storage.assign(v.data() + qmark + 1, v.size() - qmark - 1);
            stream->req.set_path(stream->path_storage);
            stream->req.set_query(stream->query_storage);
        } else {
            stream->path_storage.assign(v.data(), v.size());
            stream->query_storage.clear();
            stream->req.set_path(stream->path_storage);
            stream->req.set_query("");
        }
    } else if (n == ":authority") {
        stream->header_storage.emplace_back("Host", std::string(v));
        const auto& back = stream->header_storage.back();
        stream->req.headers().add(back.first, back.second);
    } else if (n.starts_with(':')) {
        // Other pseudo headers (:scheme, etc.)
    } else {
        stream->header_storage.emplace_back(std::string(n), std::string(v));
        const auto& back = stream->header_storage.back();
        stream->req.headers().add(back.first, back.second);
    }

    return 0;
}

int Http3Connection::on_stream_end(int64_t stream_id) {
    auto* stream = get_or_create_stream(stream_id);
    stream->request_complete = true;
    pending_dispatch_.push_back(stream_id);
    return 0;
}

int Http3Connection::on_stream_data(int64_t stream_id, const uint8_t* data, size_t datalen) {
    auto* stream = get_or_create_stream(stream_id);
    stream->body_accum.append(reinterpret_cast<const char*>(data), datalen);
    return 0;
}

int Http3Connection::on_stream_close(int64_t stream_id, uint64_t) {
    streams_.erase(stream_id);
    return 0;
}

nghttp3_ssize Http3Connection::on_stream_read(int64_t stream_id, uint32_t* pflags, nghttp3_vec* vec, size_t veccnt) {
    if (veccnt == 0) return 0;

    auto it = streams_.find(stream_id);
    if (it == streams_.end()) return NGHTTP3_ERR_CALLBACK_FAILURE;

    auto* stream = it->second.get();
    std::string_view body = stream->res.body();
    size_t available = (stream->body_offset < body.size()) ? (body.size() - stream->body_offset) : 0;

    vec[0].base = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(body.data() + stream->body_offset));
    vec[0].len = available;
    stream->body_offset += available;

    if (stream->body_offset >= body.size()) {
        *pflags |= NGHTTP3_DATA_FLAG_EOF;
    }

    return 1;
}

void Http3Connection::submit_response(Http3Stream* stream) {
    std::string status_str = std::to_string(static_cast<uint16_t>(stream->res.status()));
    std::string cl_str = std::to_string(stream->res.body().size());

    std::vector<nghttp3_nv> nva;
    nva.reserve(4 + stream->res.headers().size());

    nva.push_back(nghttp3_nv{
        .name = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":status")),
        .value = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(status_str.data())),
        .namelen = 7,
        .valuelen = status_str.size(),
        .flags = NGHTTP3_NV_FLAG_NONE
    });

    if (!stream->res.headers().contains("content-length")) {
        nva.push_back(nghttp3_nv{
            .name = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>("content-length")),
            .value = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(cl_str.data())),
            .namelen = 14,
            .valuelen = cl_str.size(),
            .flags = NGHTTP3_NV_FLAG_NONE
        });
    }

    for (const auto& h : stream->res.headers()) {
        nva.push_back(nghttp3_nv{
            .name = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(h.name.data())),
            .value = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(h.value.data())),
            .namelen = h.name.size(),
            .valuelen = h.value.size(),
            .flags = NGHTTP3_NV_FLAG_NONE
        });
    }

    nghttp3_data_reader dr{};
    dr.read_data = h3_read_data;

    nghttp3_conn_submit_response(h3conn_, stream->stream_id, nva.data(), nva.size(), &dr);
    stream->response_submitted = true;
}

core::Task<void> Http3Connection::dispatch_pending_requests() {
    if (pending_dispatch_.empty()) co_return;

    auto ready = std::move(pending_dispatch_);
    pending_dispatch_.clear();

    for (int64_t sid : ready) {
        auto it = streams_.find(sid);
        if (it == streams_.end()) continue;
        auto* stream = it->second.get();
        if (stream->response_submitted) continue;

        if (!stream->body_accum.empty()) {
            stream->req.set_body(stream->body_accum);
        }

        auto match_res = router_.match(stream->req);
        if (match_res.route_found && match_res.handler) {
            Context ctx(stream->req, stream->res, user_state_);
            co_await (*match_res.handler)(ctx);
        } else if (match_res.method_not_allowed) {
            stream->res.status(StatusCode::MethodNotAllowed).text("Method Not Allowed");
        } else {
            stream->res.status(StatusCode::NotFound).text("Not Found");
        }

        submit_response(stream);
    }
}

core::Task<bool> Http3Connection::flush_outbound() {
    if (!qconn_) co_return false;

    uint8_t out[1452];
    ngtcp2_path_storage ps;
    ngtcp2_path_storage_init(&ps, reinterpret_cast<const ngtcp2_sockaddr*>(&local_addr_), local_addr_len_,
                             reinterpret_cast<const ngtcp2_sockaddr*>(&remote_addr_), remote_addr_len_, nullptr);

    while (true) {
        int64_t stream_id = -1;
        int fin = 0;
        nghttp3_vec vec[16];
        nghttp3_ssize veccnt = 0;

        if (h3conn_) {
            veccnt = nghttp3_conn_writev_stream(h3conn_, &stream_id, &fin, vec, 16);
            if (veccnt < 0) {
                closed_ = true;
                co_return false;
            }
        }

        ngtcp2_ssize pdatalen = 0;
        uint32_t flags = (fin ? NGTCP2_WRITE_STREAM_FLAG_FIN : 0);
        ngtcp2_pkt_info pi{};
        ngtcp2_ssize nwrite = 0;

        if (stream_id >= 0) {
            nwrite = ngtcp2_conn_writev_stream(
                qconn_, &ps.path, &pi, out, sizeof(out), &pdatalen,
                flags, stream_id, reinterpret_cast<const ngtcp2_vec*>(vec), static_cast<size_t>(veccnt), get_timestamp_ns());
        } else {
            nwrite = ngtcp2_conn_writev_stream(
                qconn_, &ps.path, &pi, out, sizeof(out), &pdatalen,
                NGTCP2_WRITE_STREAM_FLAG_NONE, -1, nullptr, 0, get_timestamp_ns());
        }

        if (nwrite <= 0) {
            break;
        }

        if (h3conn_ && stream_id >= 0 && pdatalen >= 0) {
            int rv = nghttp3_conn_add_write_offset(h3conn_, stream_id, static_cast<size_t>(pdatalen));
            if (rv != 0) {
                closed_ = true;
                co_return false;
            }
        }

        ssize_t s = sendto(udp_fd_, out, static_cast<size_t>(nwrite), 0,
                           reinterpret_cast<const sockaddr*>(&remote_addr_), remote_addr_len_);
        if (s <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            co_return false;
        }
    }
    co_return true;
}

core::Task<bool> Http3Connection::feed_datagram(std::span<const uint8_t> pkt) {
    if (!qconn_) co_return false;

    ngtcp2_path path{};
    path.local.addr = const_cast<sockaddr*>(reinterpret_cast<const sockaddr*>(&local_addr_));
    path.local.addrlen = local_addr_len_;
    path.remote.addr = const_cast<sockaddr*>(reinterpret_cast<const sockaddr*>(&remote_addr_));
    path.remote.addrlen = remote_addr_len_;

    ngtcp2_pkt_info pi{};
    int rv = ngtcp2_conn_read_pkt(qconn_, &path, &pi, pkt.data(), pkt.size(), get_timestamp_ns());
    if (rv == NGTCP2_ERR_DRAINING || rv == NGTCP2_ERR_CLOSING) {
        closed_ = true;
        co_return true;
    } else if (rv != 0) {
        closed_ = true;
        co_return false;
    }

    co_await dispatch_pending_requests();
    co_return co_await flush_outbound();
}

bool Http3Connection::is_closed() const noexcept {
    return closed_ || (qconn_ && (ngtcp2_conn_in_closing_period2(qconn_) || ngtcp2_conn_in_draining_period2(qconn_)));
}

} // namespace aegon::http::v3
