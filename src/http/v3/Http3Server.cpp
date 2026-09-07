#include "http/v3/Http3Server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <openssl/rand.h>

namespace aegon::http::v3 {

Http3Server::Http3Server(core::EventLoop& loop, uint16_t port, const Router& router,
                         SSL_CTX* ssl_ctx, void* user_state, data::orm::sql::SqlDatabaseClient* sql_client)
    : loop_(loop), port_(port), router_(router), ssl_ctx_(ssl_ctx), user_state_(user_state), sql_client_(sql_client) {}

Http3Server::~Http3Server() {
    stop();
}

bool Http3Server::start() {
    udp_fd_ = socket(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    bool is_v6 = (udp_fd_ >= 0);
    if (is_v6) {
        int opt_v6only = 0; // Dual-stack IPv6 & IPv4
        setsockopt(udp_fd_, IPPROTO_IPV6, IPV6_V6ONLY, &opt_v6only, sizeof(opt_v6only));
        int reuse = 1;
        setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

        sockaddr_in6 addr6{};
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons(port_);

        if (bind(udp_fd_, reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6)) < 0) {
            ::close(udp_fd_);
            udp_fd_ = -1;
            is_v6 = false;
        }
    }

    if (!is_v6) {
        udp_fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (udp_fd_ < 0) return false;

        int reuse = 1;
        setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

        sockaddr_in addr4{};
        addr4.sin_family = AF_INET;
        addr4.sin_addr.s_addr = htonl(INADDR_ANY);
        addr4.sin_port = htons(port_);

        if (bind(udp_fd_, reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4)) < 0) {
            ::close(udp_fd_);
            udp_fd_ = -1;
            return false;
        }
    }

    running_ = true;
    return true;
}

void Http3Server::stop() noexcept {
    if (!running_) return;
    running_ = false;

    // Gracefully signal GOAWAY to all active connections
    for (auto& [_, conn] : connections_) {
        if (conn && !conn->is_closed()) {
            conn->shutdown();
            conn->flush_outbound();
        }
    }
    connections_.clear();

    // Wake up io_uring recvmsg before closing socket
    if (port_ > 0) {
        int poke_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (poke_fd >= 0) {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(port_);
            inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
            sendto(poke_fd, "q", 1, 0, reinterpret_cast<sockaddr*>(&a), sizeof(a));
            close(poke_fd);
        }
    }

    if (udp_fd_ >= 0) {
        ::close(udp_fd_);
        udp_fd_ = -1;
    }
}

void Http3Server::send_version_negotiation(const ngtcp2_version_cid& vc,
                                          const sockaddr_storage& remote_addr,
                                          socklen_t remote_addr_len) {
    uint8_t out[1200];
    uint8_t rand_byte = 0;
    RAND_bytes(&rand_byte, 1);
    const uint32_t sv[] = { NGTCP2_PROTO_VER_V1, NGTCP2_PROTO_VER_V2 };

    ngtcp2_ssize nwrite = ngtcp2_pkt_write_version_negotiation(
        out, sizeof(out), rand_byte,
        vc.scid, vc.scidlen,
        vc.dcid, vc.dcidlen,
        sv, sizeof(sv) / sizeof(sv[0])
    );

    if (nwrite > 0 && udp_fd_ >= 0) {
        sendto(udp_fd_, out, static_cast<size_t>(nwrite), 0,
               reinterpret_cast<const sockaddr*>(&remote_addr), remote_addr_len);
    }
}

void Http3Server::check_expiries() {
    uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());

    std::vector<Http3Connection*> ticked;
    ticked.reserve(connections_.size());

    for (auto& [_, conn] : connections_) {
        if (!conn || conn->is_closed()) continue;
        auto* raw = conn.get();
        if (std::find(ticked.begin(), ticked.end(), raw) != ticked.end()) {
            continue;
        }
        ticked.push_back(raw);

        if (now >= raw->get_expiry()) {
            raw->handle_expiry();
        }
    }
}

void Http3Server::prune_connections() {
    std::erase_if(connections_, [](const auto& pair) {
        return !pair.second || pair.second->is_closed();
    });
}

core::Task<void> Http3Server::run_timer_loop() {
    while (running_) {
        co_await loop_.ring().timeout(10'000'000ULL); // 10ms tick for RFC 9002 loss detection & PTO
        if (!running_) break;
        check_expiries();
        prune_connections();
    }
}

core::Task<void> Http3Server::run_receive_loop() {
    alignas(64) uint8_t buf[65536];
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
        if (bytes <= 0 || !running_) {
            if (!running_) break;
            continue;
        }

        std::span<const uint8_t> pkt(buf, static_cast<size_t>(bytes));
        if (!QuicHeader::is_quic_packet(pkt)) {
            if (!running_) break;
            continue;
        }

        ngtcp2_version_cid vc{};
        int rv = ngtcp2_pkt_decode_version_cid(&vc, pkt.data(), pkt.size(), 16);
        if (rv != 0 && rv != NGTCP2_ERR_VERSION_NEGOTIATION) {
            continue;
        }

        // RFC 9000 §5.2: Send Version Negotiation packet for unsupported versions
        if (rv == NGTCP2_ERR_VERSION_NEGOTIATION ||
            (vc.version != 0 && vc.version != NGTCP2_PROTO_VER_V1 && vc.version != NGTCP2_PROTO_VER_V2)) {
            send_version_negotiation(vc, remote_addr, msg.msg_namelen);
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
                loop_, udp_fd_, remote_addr, msg.msg_namelen, router_, ssl_ctx_, user_state_, sql_client_);

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
        }
    }
}

} // namespace aegon::http::v3
