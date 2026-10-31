#include "net/tcp/TcpListener.h"

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace aegon::net {

std::optional<TcpListener> TcpListener::bind(const SocketAddress& addr, const TcpListenConfig& cfg) {
    if (!addr.is_valid()) return std::nullopt;

    core::init_sockets();
    int family = addr.family();

#if defined(_WIN32)
    core::os_socket_t fd = WSASocketW(family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
#elif defined(SOCK_CLOEXEC)
    core::os_socket_t fd = ::socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    core::os_socket_t fd = ::socket(family, SOCK_STREAM, 0);
#endif

    if (!core::is_valid_socket(fd)) {
        return std::nullopt;
    }

    if (cfg.reuse_addr) {
        SocketOptions::set_reuse_address(fd, true);
    }
    if (cfg.reuse_port) {
        SocketOptions::set_reuse_port(fd, true);
    }
    if (cfg.tcp_nodelay) {
        SocketOptions::set_tcp_nodelay(fd, true);
    }
    if (cfg.tcp_keepalive) {
        SocketOptions::set_tcp_keepalive(fd, true, cfg.keepidle, cfg.keepintvl, cfg.keepcnt);
    }
    if (cfg.defer_accept) {
        SocketOptions::set_tcp_defer_accept(fd, 1);
    }

    if (family == AF_INET6) {
        // Dual-stack IPv6/IPv4 support
        SocketOptions::set_ipv6_only(fd, false);
    }

    if (::bind(fd, addr.as_sockaddr(), addr.length()) < 0) {
        core::close_socket(fd);
        return std::nullopt;
    }

    int backlog = cfg.backlog > 0 ? cfg.backlog : 32768;
    if (::listen(fd, backlog) < 0) {
        core::close_socket(fd);
        return std::nullopt;
    }

    SocketAddress actual_addr = addr;
    socklen_t actual_len = sizeof(sockaddr_storage);
    if (::getsockname(fd, actual_addr.as_sockaddr(), &actual_len) == 0) {
        actual_addr.set_length(actual_len);
    }

    return TcpListener(fd, actual_addr);
}

std::optional<TcpListener> TcpListener::bind(uint16_t port, std::string_view host, const TcpListenConfig& cfg) {
    TcpListenConfig config = cfg;
    config.port = port;
    config.host = std::string(host);

    std::optional<SocketAddress> addr;
    if (host == "0.0.0.0" || host == "*") {
        addr = SocketAddress::any_ipv4(port);
    } else if (host == "::" || host == "[::]") {
        addr = SocketAddress::any_ipv6(port);
    } else {
        addr = SocketAddress::parse(host, port);
    }

    if (!addr) {
        return std::nullopt;
    }

    return bind(*addr, config);
}

core::Task<std::optional<TcpStream>> TcpListener::accept(core::IoEngine& engine) {
    if (!is_valid()) co_return std::nullopt;

    auto result = co_await engine.accept_once(fd_);
    if (!core::is_valid_socket(result.fd) || result.fd < 0) {
        co_return std::nullopt;
    }

    co_return TcpStream(engine, result.fd);
}

} // namespace aegon::net
