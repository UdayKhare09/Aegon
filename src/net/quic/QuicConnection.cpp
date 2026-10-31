#include "net/quic/QuicConnection.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <openssl/rand.h>

#if defined(__linux__)
#include <sys/socket.h>
#include <netinet/udp.h>
#ifndef UDP_SEGMENT
#define UDP_SEGMENT 103
#endif
#ifndef SOL_UDP
#define SOL_UDP 17
#endif
#endif

namespace aegon::net::quic {

namespace {

uint64_t get_timestamp_ns() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

} // anonymous namespace

QuicConnection::QuicConnection(core::EventLoop& loop, core::os_socket_t udp_fd,
                               const sockaddr_storage& remote_addr, socklen_t remote_addr_len,
                               SSL_CTX* ssl_ctx)
    : loop_(loop), udp_fd_(udp_fd), remote_addr_(remote_addr), remote_addr_len_(remote_addr_len),
      ssl_ctx_(ssl_ctx) {}

QuicConnection::~QuicConnection() {
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

void QuicConnection::add_source_conn_id(std::string_view cid) {
    scids_.emplace_back(cid);
    if (on_cid_added_) {
        on_cid_added_(cid);
    }
}

void QuicConnection::remove_source_conn_id(std::string_view cid) {
    std::erase_if(scids_, [&](const std::string& s) { return s == cid; });
    if (on_cid_removed_) {
        on_cid_removed_(cid);
    }
}

int QuicConnection::handle_recv_stream_data(uint32_t flags, int64_t stream_id, const uint8_t* data, size_t datalen) {
    bool fin = (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0;
    if (stream_data_handler_) {
        stream_data_handler_(stream_id, std::span<const uint8_t>(data, datalen), fin);
    }
    ngtcp2_conn_extend_max_stream_offset(qconn_, stream_id, datalen);
    ngtcp2_conn_extend_max_offset(qconn_, datalen);
    return 0;
}

int QuicConnection::handle_stream_close(int64_t stream_id, uint64_t app_error_code) {
    if (stream_close_handler_) {
        stream_close_handler_(stream_id, app_error_code);
    }
    if ((stream_id & 0x03) == 0) {
        ngtcp2_conn_extend_max_streams_bidi(qconn_, 1);
    }
    return 0;
}

void QuicConnection::handle_handshake_completed() {
    if (handshake_handler_) {
        handshake_handler_();
    }
}

bool QuicConnection::init(const uint8_t* dcid, size_t dcidlen, const uint8_t* scid, size_t scidlen, uint32_t version) {
    if (!ssl_ctx_) return false;

    ssl_ = SSL_new(ssl_ctx_);
    if (!ssl_) return false;

    SSL_set_app_data(ssl_, this);
    SSL_set_accept_state(ssl_);

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

    qcb.get_new_connection_id = [](ngtcp2_conn*, ngtcp2_cid* cid, uint8_t* token,
                                   size_t cidlen, void* user_data) -> int {
        auto* self = static_cast<QuicConnection*>(user_data);
        RAND_bytes(cid->data, static_cast<int>(cidlen));
        cid->datalen = cidlen;
        RAND_bytes(token, NGTCP2_STATELESS_RESET_TOKENLEN);
        self->add_source_conn_id(std::string_view(reinterpret_cast<const char*>(cid->data), cid->datalen));
        return 0;
    };

    qcb.remove_connection_id = [](ngtcp2_conn*, const ngtcp2_cid* cid, void* user_data) -> int {
        auto* self = static_cast<QuicConnection*>(user_data);
        self->remove_source_conn_id(std::string_view(reinterpret_cast<const char*>(cid->data), cid->datalen));
        return 0;
    };

    qcb.recv_stream_data = [](ngtcp2_conn*, uint32_t flags, int64_t stream_id,
                              uint64_t, const uint8_t* data, size_t datalen,
                              void* user_data, void*) -> int {
        auto* self = static_cast<QuicConnection*>(user_data);
        return self->handle_recv_stream_data(flags, stream_id, data, datalen);
    };

    qcb.stream_close = [](ngtcp2_conn*, uint32_t, int64_t stream_id,
                          uint64_t app_error_code, void* user_data, void*) -> int {
        auto* self = static_cast<QuicConnection*>(user_data);
        return self->handle_stream_close(stream_id, app_error_code);
    };

    qcb.handshake_completed = [](ngtcp2_conn*, void* user_data) -> int {
        auto* self = static_cast<QuicConnection*>(user_data);
        self->handle_handshake_completed();
        return 0;
    };

    ngtcp2_settings qsettings{};
    ngtcp2_settings_default(&qsettings);
    qsettings.initial_ts = get_timestamp_ns();
    qsettings.max_tx_udp_payload_size = 1452;

    ngtcp2_transport_params qparams{};
    ngtcp2_transport_params_default(&qparams);
    qparams.max_udp_payload_size = 1452;
    qparams.initial_max_streams_bidi = 65535;
    qparams.initial_max_streams_uni = 65535;
    qparams.initial_max_stream_data_bidi_remote = 10485760;
    qparams.initial_max_stream_data_bidi_local = 10485760;
    qparams.initial_max_stream_data_uni = 10485760;
    qparams.initial_max_data = 104857600;

    local_addr_len_ = sizeof(local_addr_);
    if (udp_fd_ < 0 || getsockname(udp_fd_, reinterpret_cast<sockaddr*>(&local_addr_), &local_addr_len_) != 0) {
        auto* sin = reinterpret_cast<sockaddr_in*>(&local_addr_);
        sin->sin_family = AF_INET;
        sin->sin_port = htons(443);
        sin->sin_addr.s_addr = htonl(INADDR_ANY);
        local_addr_len_ = sizeof(sockaddr_in);
    }

    ngtcp2_path path{};
    path.local.addr = const_cast<sockaddr*>(reinterpret_cast<const sockaddr*>(&local_addr_));
    path.local.addrlen = local_addr_len_;
    path.remote.addr = const_cast<sockaddr*>(reinterpret_cast<const sockaddr*>(&remote_addr_));
    path.remote.addrlen = remote_addr_len_;

    uint8_t local_scid[16];
    RAND_bytes(local_scid, sizeof(local_scid));
    scids_.emplace_back(reinterpret_cast<const char*>(local_scid), sizeof(local_scid));

    ngtcp2_cid qdcid, qscid;
    ngtcp2_cid_init(&qdcid, scid, scidlen);
    ngtcp2_cid_init(&qscid, local_scid, sizeof(local_scid));

    int rv = ngtcp2_conn_server_new(&qconn_, &qdcid, &qscid, &path, version,
                                    &qcb, &qsettings, &qparams, nullptr, this);
    if (rv != 0) {
        return false;
    }

    rv = ngtcp2_crypto_ossl_ctx_new(&ossl_ctx_, ssl_);
    if (rv != 0) {
        return false;
    }

    ngtcp2_conn_set_tls_native_handle(qconn_, ossl_ctx_);
    return true;
}

bool QuicConnection::feed_datagram(std::span<const uint8_t> pkt, const sockaddr_storage& from_addr, socklen_t from_len) {
    if (!qconn_) return false;

    remote_addr_ = from_addr;
    remote_addr_len_ = from_len;

    ngtcp2_path path{};
    path.local.addr = reinterpret_cast<sockaddr*>(&local_addr_);
    path.local.addrlen = local_addr_len_;
    path.remote.addr = const_cast<sockaddr*>(reinterpret_cast<const sockaddr*>(&from_addr));
    path.remote.addrlen = from_len;

    ngtcp2_pkt_info pi{};
    int rv = ngtcp2_conn_read_pkt(qconn_, &path, &pi, pkt.data(), pkt.size(), get_timestamp_ns());
    if (rv != 0) {
        return false;
    }

    return flush_outbound();
}

int64_t QuicConnection::open_bidi_stream() {
    if (!qconn_) return -1;
    int64_t stream_id = -1;
    if (ngtcp2_conn_open_bidi_stream(qconn_, &stream_id, nullptr) != 0) {
        return -1;
    }
    return stream_id;
}

int64_t QuicConnection::open_uni_stream() {
    if (!qconn_) return -1;
    int64_t stream_id = -1;
    if (ngtcp2_conn_open_uni_stream(qconn_, &stream_id, nullptr) != 0) {
        return -1;
    }
    return stream_id;
}

bool QuicConnection::write_stream(int64_t stream_id, std::span<const uint8_t> data, bool fin) {
    if (!qconn_ || stream_id < 0) return false;

    StreamOutEntry entry;
    entry.stream_id = stream_id;
    entry.data.assign(data.begin(), data.end());
    entry.fin = fin;
    stream_send_queue_.push_back(std::move(entry));

    return flush_outbound();
}

bool QuicConnection::flush_outbound() {
    if (!qconn_ || is_closed() || udp_fd_ == core::INVALID_OS_SOCKET) return false;

    uint8_t out[65536];
    ngtcp2_path_storage ps;
    ngtcp2_path_storage_zero(&ps);

    while (true) {
        ngtcp2_pkt_info pi{};
        ngtcp2_ssize nwrite = 0;

        if (!stream_send_queue_.empty()) {
            auto& front = stream_send_queue_.front();
            uint32_t flags = front.fin ? NGTCP2_WRITE_STREAM_FLAG_FIN : NGTCP2_WRITE_STREAM_FLAG_NONE;
            ngtcp2_vec datavec{};
            datavec.base = front.data.data() + front.offset;
            datavec.len = front.data.size() - front.offset;

            ngtcp2_ssize ndatalen = 0;
            nwrite = ngtcp2_conn_writev_stream(qconn_, &ps.path, &pi, out, sizeof(out),
                                               &ndatalen, flags, front.stream_id,
                                               &datavec, 1, get_timestamp_ns());
            if (ndatalen > 0) {
                front.offset += static_cast<size_t>(ndatalen);
                if (front.offset >= front.data.size()) {
                    stream_send_queue_.pop_front();
                }
            }
        } else {
            nwrite = ngtcp2_conn_write_pkt(qconn_, &ps.path, &pi, out, sizeof(out), get_timestamp_ns());
        }

        if (nwrite <= 0) break;

        ::sendto(udp_fd_, reinterpret_cast<const char*>(out), static_cast<int>(nwrite), 0,
                 reinterpret_cast<const sockaddr*>(&remote_addr_), remote_addr_len_);
    }

    return true;
}

uint64_t QuicConnection::get_expiry() const noexcept {
    if (!qconn_) return UINT64_MAX;
    return ngtcp2_conn_get_expiry(qconn_);
}

bool QuicConnection::handle_expiry() {
    if (!qconn_) return false;
    uint64_t now = get_timestamp_ns();
    if (ngtcp2_conn_handle_expiry(qconn_, now) != 0) {
        return false;
    }
    return flush_outbound();
}

void QuicConnection::shutdown(uint64_t app_error_code) {
    if (!qconn_ || is_closed()) return;
    ngtcp2_ccerr ccerr{};
    ngtcp2_ccerr_set_application_error(&ccerr, app_error_code, nullptr, 0);
    uint8_t out[1500];
    ngtcp2_path_storage ps;
    ngtcp2_path_storage_zero(&ps);
    ngtcp2_pkt_info pi{};
    ngtcp2_ssize nwrite = ngtcp2_conn_write_connection_close(
        qconn_, &ps.path, &pi, out, sizeof(out), &ccerr, get_timestamp_ns());
    if (nwrite > 0 && udp_fd_ != core::INVALID_OS_SOCKET) {
        ::sendto(udp_fd_, reinterpret_cast<const char*>(out), static_cast<int>(nwrite), 0,
                 reinterpret_cast<const sockaddr*>(&remote_addr_), remote_addr_len_);
    }
}

bool QuicConnection::is_closed() const noexcept {
    if (!qconn_) return true;
    return ngtcp2_conn_in_closing_period2(qconn_) || ngtcp2_conn_in_draining_period2(qconn_);
}

} // namespace aegon::net::quic
