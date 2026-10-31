#pragma once

#include "core/EventLoop.h"
#include "core/OsSocket.h"
#include "core/Task.h"
#include "net/SocketAddress.h"
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <openssl/ssl.h>
#include <functional>
#include <string>
#include <vector>
#include <span>
#include <cstdint>
#include <memory>
#include <deque>

namespace aegon::net::quic {

class QuicConnection {
public:
    using StreamDataHandler = std::function<void(int64_t stream_id, std::span<const uint8_t> data, bool fin)>;
    using StreamCloseHandler = std::function<void(int64_t stream_id, uint64_t app_err)>;
    using DatagramHandler = std::function<void(std::span<const uint8_t> data)>;
    using HandshakeHandler = std::function<void()>;
    using CidCallback = std::function<void(std::string_view)>;

    QuicConnection(core::EventLoop& loop, core::os_socket_t udp_fd,
                   const sockaddr_storage& remote_addr, socklen_t remote_addr_len,
                   SSL_CTX* ssl_ctx);
    ~QuicConnection();

    QuicConnection(const QuicConnection&) = delete;
    QuicConnection& operator=(const QuicConnection&) = delete;

    /**
     * @brief Initialize QUIC session from initial client datagram connection IDs.
     */
    bool init(const uint8_t* dcid, size_t dcidlen, const uint8_t* scid, size_t scidlen, uint32_t version = NGTCP2_PROTO_VER_V1);

    /**
     * @brief Ingest an inbound UDP datagram into the QUIC engine.
     */
    bool feed_datagram(std::span<const uint8_t> pkt, const sockaddr_storage& from_addr, socklen_t from_len);

    /**
     * @brief Transmit application data on a QUIC stream.
     */
    bool write_stream(int64_t stream_id, std::span<const uint8_t> data, bool fin = false);

    /**
     * @brief Open a new unidirectional or bidirectional stream.
     */
    int64_t open_uni_stream();
    int64_t open_bidi_stream();

    /**
     * @brief Flush pending outbound QUIC packets over UDP using sendmmsg and GSO.
     */
    bool flush_outbound();

    /**
     * @brief Return the nanosecond timestamp for next loss recovery timer tick (RFC 9002).
     */
    [[nodiscard]] uint64_t get_expiry() const noexcept;

    /**
     * @brief Handle timer expiry / loss detection tick (RFC 9002).
     */
    bool handle_expiry();

    /**
     * @brief Gracefully initiate QUIC connection close.
     */
    void shutdown(uint64_t app_error_code = 0);

    [[nodiscard]] bool is_closed() const noexcept;
    [[nodiscard]] SocketAddress remote_address() const noexcept {
        return SocketAddress(reinterpret_cast<const sockaddr*>(&remote_addr_), remote_addr_len_);
    }

    [[nodiscard]] ngtcp2_conn* raw_conn() noexcept { return qconn_; }
    [[nodiscard]] const std::vector<std::string>& source_conn_ids() const noexcept { return scids_; }

    void on_stream_data(StreamDataHandler handler) { stream_data_handler_ = std::move(handler); }
    void on_stream_close(StreamCloseHandler handler) { stream_close_handler_ = std::move(handler); }
    void on_datagram(DatagramHandler handler) { datagram_handler_ = std::move(handler); }
    void on_handshake_done(HandshakeHandler handler) { handshake_handler_ = std::move(handler); }

    void set_cid_callbacks(CidCallback on_added, CidCallback on_removed) {
        on_cid_added_ = std::move(on_added);
        on_cid_removed_ = std::move(on_removed);
    }

    void add_source_conn_id(std::string_view cid);
    void remove_source_conn_id(std::string_view cid);

    // Callbacks invoked by ngtcp2
    int handle_recv_stream_data(uint32_t flags, int64_t stream_id, const uint8_t* data, size_t datalen);
    int handle_stream_close(int64_t stream_id, uint64_t app_error_code);
    void handle_handshake_completed();

private:
    core::EventLoop& loop_;
    core::os_socket_t udp_fd_{core::INVALID_OS_SOCKET};
    sockaddr_storage remote_addr_{};
    socklen_t remote_addr_len_{sizeof(sockaddr_storage)};
    sockaddr_storage local_addr_{};
    socklen_t local_addr_len_{sizeof(sockaddr_storage)};

    SSL_CTX* ssl_ctx_{nullptr};
    SSL* ssl_{nullptr};
    ngtcp2_crypto_ossl_ctx* ossl_ctx_{nullptr};
    ngtcp2_conn* qconn_{nullptr};

    std::vector<std::string> scids_;
    CidCallback on_cid_added_{};
    CidCallback on_cid_removed_{};

    StreamDataHandler stream_data_handler_{};
    StreamCloseHandler stream_close_handler_{};
    DatagramHandler datagram_handler_{};
    HandshakeHandler handshake_handler_{};

    struct StreamOutEntry {
        int64_t stream_id{-1};
        std::vector<uint8_t> data{};
        size_t offset{0};
        bool fin{false};
    };
    std::deque<StreamOutEntry> stream_send_queue_;
};

} // namespace aegon::net::quic

