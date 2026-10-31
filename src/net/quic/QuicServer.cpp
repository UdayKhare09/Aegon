#include "net/quic/QuicServer.h"
#include <chrono>
#include <cstring>
#include <iostream>

namespace aegon::net::quic {

QuicServer::QuicServer(core::EventLoop& loop, uint16_t port, tls::TlsContext& tls_ctx, std::string_view host)
    : loop_(loop), port_(port), host_(host), tls_ctx_(tls_ctx) {}

QuicServer::~QuicServer() {
    stop();
}

bool QuicServer::start() {
    auto sock = UdpSocket::bind(port_, host_, true);
    if (!sock) {
        return false;
    }
    socket_ = std::move(*sock);
    socket_.set_buffer_sizes(4 * 1024 * 1024, 4 * 1024 * 1024);

    running_ = true;
    loop_.spawn(run_receive_loop());
    loop_.spawn(run_timer_loop());
    return true;
}

void QuicServer::stop() noexcept {
    if (running_.exchange(false)) {
        for (auto& [_, conn] : connections_) {
            if (conn) {
                conn->shutdown(0);
            }
        }
        connections_.clear();
        socket_.close();
    }
}

void QuicServer::dispatch_datagram(std::span<const uint8_t> pkt, const sockaddr_storage& from, socklen_t from_len) {
    if (!QuicHeader::is_quic_packet(pkt)) return;

    ngtcp2_version_cid vc{};
    int rv = ngtcp2_pkt_decode_version_cid(&vc, pkt.data(), pkt.size(), 16);
    if (rv != 0) return;

    std::string dcid_key(reinterpret_cast<const char*>(vc.dcid), vc.dcidlen);
    auto it = connections_.find(dcid_key);
    if (it != connections_.end() && it->second) {
        it->second->feed_datagram(pkt, from, from_len);
        return;
    }

    // New connection: Initial packet
    if (vc.version != 0 && vc.version != NGTCP2_PROTO_VER_V1) {
        // Version negotiation would happen here
        return;
    }

    auto conn = std::make_shared<QuicConnection>(loop_, socket_.native_handle(), from, from_len, tls_ctx_.native_handle());
    if (stream_handler_) {
        conn->on_stream_data([this, weak = std::weak_ptr<QuicConnection>(conn)](int64_t stream_id, std::span<const uint8_t> data, bool fin) {
            if (auto sp = weak.lock()) {
                stream_handler_(*sp, stream_id, data, fin);
            }
        });
    }

    conn->set_cid_callbacks(
        [this, sp = conn](std::string_view cid) {
            connections_[std::string(cid)] = sp;
        },
        [this](std::string_view cid) {
            connections_.erase(std::string(cid));
        }
    );

    if (!conn->init(vc.dcid, vc.dcidlen, vc.scid, vc.scidlen, vc.version)) {
        return;
    }

    connections_[dcid_key] = conn;
    if (conn_handler_) {
        conn_handler_(*conn);
    }

    conn->feed_datagram(pkt, from, from_len);
}

core::Task<void> QuicServer::run_receive_loop() {
    alignas(64) uint8_t buf[65536];
    SocketAddress from;

    while (running_ && socket_.is_valid()) {
        int n = co_await socket_.recvfrom(loop_.engine(), buf, sizeof(buf), from);
        if (n <= 0) {
            if (!running_) break;
            continue;
        }

        dispatch_datagram(std::span<const uint8_t>(buf, static_cast<size_t>(n)), from.storage(), from.length());
    }
}

core::Task<void> QuicServer::run_timer_loop() {
    while (running_) {
        co_await loop_.engine().timeout(10'000'000ULL); // 10ms tick for loss recovery
        if (!running_) break;

        uint64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        for (auto it = connections_.begin(); it != connections_.end();) {
            if (!it->second || it->second->is_closed()) {
                it = connections_.erase(it);
                continue;
            }
            if (it->second->get_expiry() <= now) {
                it->second->handle_expiry();
            }
            ++it;
        }
    }
}

} // namespace aegon::net::quic
