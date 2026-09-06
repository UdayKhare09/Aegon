#include "http/v3/Http3Server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

namespace aegon::http::v3 {

Http3Server::Http3Server(core::EventLoop& loop, uint16_t port, const Router& router,
                         SSL_CTX* ssl_ctx, void* user_state)
    : loop_(loop), port_(port), router_(router), ssl_ctx_(ssl_ctx), user_state_(user_state) {}

Http3Server::~Http3Server() {
    stop();
}

bool Http3Server::start() {
    udp_fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (udp_fd_ < 0) {
        return false;
    }

    int opt = 1;
    setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    if (bind(udp_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(udp_fd_);
        udp_fd_ = -1;
        return false;
    }

    running_ = true;
    return true;
}

void Http3Server::stop() noexcept {
    running_ = false;
    if (udp_fd_ >= 0) {
        ::close(udp_fd_);
        udp_fd_ = -1;
    }
}

core::Task<void> Http3Server::run_receive_loop() {
    alignas(64) uint8_t buf[2048];
    sockaddr_storage remote_addr{};
    iovec iov{.iov_base = buf, .iov_len = sizeof(buf)};
    msghdr msg{};

    while (running_) {
        msg.msg_name = &remote_addr;
        msg.msg_namelen = sizeof(remote_addr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;

        int bytes = co_await loop_.ring().recvmsg(udp_fd_, &msg);
        if (bytes <= 0) {
            if (!running_) break;
            continue;
        }

        std::span<const uint8_t> pkt(buf, static_cast<size_t>(bytes));
        if (!QuicHeader::is_quic_packet(pkt)) {
            continue;
        }

        ngtcp2_version_cid vc{};
        int rv = ngtcp2_pkt_decode_version_cid(&vc, pkt.data(), pkt.size(), 16);
        if (rv != 0 && rv != NGTCP2_ERR_VERSION_NEGOTIATION) {
            continue;
        }

        std::string dcid_key(reinterpret_cast<const char*>(vc.dcid), vc.dcidlen);
        auto it = connections_.find(dcid_key);
        std::shared_ptr<Http3Connection> conn = nullptr;

        if (it != connections_.end()) {
            conn = it->second;
        } else if (vc.version != 0 && vc.scidlen > 0) {
            // New connection triggered by client Initial packet
            auto new_conn = std::make_shared<Http3Connection>(
                loop_, udp_fd_, remote_addr, msg.msg_namelen, router_, ssl_ctx_, user_state_);

            if (new_conn->init(vc.dcid, vc.dcidlen, vc.scid, vc.scidlen)) {
                conn = new_conn;
                connections_[dcid_key] = conn;

                // Also map server-generated SCID to this connection
                for (const auto& scid_str : conn->source_conn_ids()) {
                    connections_[scid_str] = conn;
                }
            }
        }

        if (conn) {
            co_await conn->feed_datagram(pkt);

            // Register any new source connection IDs negotiated
            for (const auto& scid_str : conn->source_conn_ids()) {
                connections_.try_emplace(scid_str, conn);
            }

            if (conn->is_closed()) {
                // Connection will be pruned
            }
        }
    }
}

} // namespace aegon::http::v3
