#include "net/SocketOptions.h"

#if defined(_WIN32)
#include <ws2tcpip.h>
#include <mstcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace aegon::net {

bool SocketOptions::set_nonblocking(core::os_socket_t fd, bool nonblocking) noexcept {
    if (!core::is_valid_socket(fd)) return false;
#if defined(_WIN32)
    u_long mode = nonblocking ? 1 : 0;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    flags = nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return ::fcntl(fd, F_SETFL, flags) == 0;
#endif
}

bool SocketOptions::set_reuse_address(core::os_socket_t fd, bool reuse) noexcept {
    if (!core::is_valid_socket(fd)) return false;
    int opt = reuse ? 1 : 0;
    return ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
}

bool SocketOptions::set_reuse_port(core::os_socket_t fd, bool reuse) noexcept {
    if (!core::is_valid_socket(fd)) return false;
#if defined(SO_REUSEPORT)
    int opt = reuse ? 1 : 0;
    return ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
#else
    (void)reuse;
    return false;
#endif
}

bool SocketOptions::set_tcp_nodelay(core::os_socket_t fd, bool nodelay) noexcept {
    if (!core::is_valid_socket(fd)) return false;
    int opt = nodelay ? 1 : 0;
    return ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
}

bool SocketOptions::set_tcp_keepalive(core::os_socket_t fd, bool enable, int idle_sec, int intvl_sec, int count) noexcept {
    if (!core::is_valid_socket(fd)) return false;
    int opt = enable ? 1 : 0;
    if (::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&opt), sizeof(opt)) != 0) {
        return false;
    }
    if (!enable) return true;

#if defined(__linux__)
#if defined(TCP_KEEPIDLE)
    if (idle_sec > 0) {
        ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, reinterpret_cast<const char*>(&idle_sec), sizeof(idle_sec));
    }
#endif
#if defined(TCP_KEEPINTVL)
    if (intvl_sec > 0) {
        ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, reinterpret_cast<const char*>(&intvl_sec), sizeof(intvl_sec));
    }
#endif
#if defined(TCP_KEEPCNT)
    if (count > 0) {
        ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, reinterpret_cast<const char*>(&count), sizeof(count));
    }
#endif
#elif defined(_WIN32)
    struct tcp_keepalive settings{};
    settings.onoff = 1;
    settings.keepalivetime = static_cast<u_long>(idle_sec * 1000);
    settings.keepaliveinterval = static_cast<u_long>(intvl_sec * 1000);
    DWORD bytes_returned = 0;
    WSAIoctl(fd, SIO_KEEPALIVE_VALS, &settings, sizeof(settings), nullptr, 0, &bytes_returned, nullptr, nullptr);
#endif
    return true;
}

bool SocketOptions::set_send_buffer_size(core::os_socket_t fd, int size_bytes) noexcept {
    if (!core::is_valid_socket(fd) || size_bytes <= 0) return false;
    return ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&size_bytes), sizeof(size_bytes)) == 0;
}

bool SocketOptions::set_recv_buffer_size(core::os_socket_t fd, int size_bytes) noexcept {
    if (!core::is_valid_socket(fd) || size_bytes <= 0) return false;
    return ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&size_bytes), sizeof(size_bytes)) == 0;
}

bool SocketOptions::set_ipv6_only(core::os_socket_t fd, bool v6only) noexcept {
    if (!core::is_valid_socket(fd)) return false;
    int opt = v6only ? 1 : 0;
    return ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
}

bool SocketOptions::set_tcp_defer_accept(core::os_socket_t fd, int timeout_sec) noexcept {
    if (!core::is_valid_socket(fd)) return false;
#if defined(__linux__) && defined(TCP_DEFER_ACCEPT)
    return ::setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, reinterpret_cast<const char*>(&timeout_sec), sizeof(timeout_sec)) == 0;
#else
    (void)timeout_sec;
    return false;
#endif
}

bool SocketOptions::set_tcp_quickack(core::os_socket_t fd, bool enable) noexcept {
    if (!core::is_valid_socket(fd)) return false;
#if defined(__linux__) && defined(TCP_QUICKACK)
    int opt = enable ? 1 : 0;
    return ::setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
#else
    (void)enable;
    return false;
#endif
}

} // namespace aegon::net

