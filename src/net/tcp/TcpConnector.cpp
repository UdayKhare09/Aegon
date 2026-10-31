#include "net/tcp/TcpConnector.h"
#include "net/SocketOptions.h"

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace aegon::net {

core::Task<std::optional<TcpStream>> TcpConnector::connect(
    core::IoEngine& engine,
    const SocketAddress& addr,
    std::chrono::milliseconds timeout) {
    (void)timeout; // Can be enhanced with timeout race awaiter if needed

    if (!addr.is_valid()) co_return std::nullopt;

    core::init_sockets();
    int family = addr.family();

#if defined(_WIN32)
    core::os_socket_t sock = WSASocketW(family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
#elif defined(SOCK_CLOEXEC)
    core::os_socket_t sock = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#else
    core::os_socket_t sock = ::socket(family, SOCK_STREAM, 0);
#endif

    if (!core::is_valid_socket(sock)) {
        co_return std::nullopt;
    }

    SocketOptions::set_nonblocking(sock, true);
    SocketOptions::set_tcp_nodelay(sock, true);

    int conn_res = co_await engine.connect(sock, addr.as_sockaddr(), addr.length());
    if (conn_res < 0) {
        core::close_socket(sock);
        co_return std::nullopt;
    }

    co_return TcpStream(engine, sock);
}

core::Task<std::optional<TcpStream>> TcpConnector::connect(
    core::IoEngine& engine,
    std::string_view host,
    uint16_t port,
    std::chrono::milliseconds timeout) {
    // Quick parse for IP literals first to avoid getaddrinfo
    if (auto direct = SocketAddress::parse(host, port)) {
        co_return co_await connect(engine, *direct, timeout);
    }

    // Hostname resolution via getaddrinfo
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* res = nullptr;
    std::string host_str(host);
    std::string port_str = std::to_string(port);

    int rc = ::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        co_return std::nullopt;
    }

    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        SocketAddress sa(p->ai_addr, static_cast<socklen_t>(p->ai_addrlen));
        auto stream_opt = co_await connect(engine, sa, timeout);
        if (stream_opt.has_value()) {
            ::freeaddrinfo(res);
            co_return stream_opt;
        }
    }

    ::freeaddrinfo(res);
    co_return std::nullopt;
}

} // namespace aegon::net

