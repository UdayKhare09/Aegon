#include "net/udp/UdpSocket.h"
#include <cstring>

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace aegon::net {

std::optional<UdpSocket> UdpSocket::bind(const SocketAddress& addr, bool reuse_port) {
    if (!addr.is_valid()) return std::nullopt;

    core::init_sockets();
    int family = addr.family();

#if defined(_WIN32)
    core::os_socket_t fd = WSASocketW(family, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
#elif defined(SOCK_CLOEXEC)
    core::os_socket_t fd = ::socket(family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
#else
    core::os_socket_t fd = ::socket(family, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
#endif

    if (!core::is_valid_socket(fd)) {
        return std::nullopt;
    }

    SocketOptions::set_nonblocking(fd, true);
    SocketOptions::set_reuse_address(fd, true);
    if (reuse_port) {
        SocketOptions::set_reuse_port(fd, true);
    }

    // Default large socket buffers for high packet rates
    SocketOptions::set_recv_buffer_size(fd, 4 * 1024 * 1024);
    SocketOptions::set_send_buffer_size(fd, 4 * 1024 * 1024);

    if (family == AF_INET6) {
        SocketOptions::set_ipv6_only(fd, false);
    }

    if (::bind(fd, addr.as_sockaddr(), addr.length()) < 0) {
        core::close_socket(fd);
        return std::nullopt;
    }

    SocketAddress actual_addr = addr;
    socklen_t actual_len = sizeof(sockaddr_storage);
    if (::getsockname(fd, actual_addr.as_sockaddr(), &actual_len) == 0) {
        actual_addr.set_length(actual_len);
    }

    return UdpSocket(fd, actual_addr);
}

std::optional<UdpSocket> UdpSocket::bind(uint16_t port, std::string_view host, bool reuse_port) {
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

    return bind(*addr, reuse_port);
}

core::Task<int> UdpSocket::recvfrom(core::IoEngine& engine, void* buf, size_t len, SocketAddress& from_addr) {
    iovec iov{};
    iov.iov_base = buf;
    iov.iov_len = len;

    msghdr msg{};
    msg.msg_name = from_addr.as_sockaddr();
    msg.msg_namelen = sizeof(sockaddr_storage);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    int n = co_await engine.recvmsg(fd_, &msg);
    if (n >= 0) {
        from_addr.set_length(msg.msg_namelen);
    }
    co_return n;
}

int UdpSocket::sendto(const void* buf, size_t len, const SocketAddress& to_addr, int flags) noexcept {
    if (!is_valid() || !to_addr.is_valid()) return -1;
    return static_cast<int>(::sendto(
        fd_,
        reinterpret_cast<const char*>(buf),
        static_cast<int>(len),
        flags,
        to_addr.as_sockaddr(),
        to_addr.length()
    ));
}

#if defined(__linux__)
int UdpSocket::send_batch(struct mmsghdr* msgs, unsigned int count, int flags) noexcept {
    if (!is_valid() || !msgs || count == 0) return 0;
    return ::sendmmsg(fd_, msgs, count, flags);
}

int UdpSocket::recv_batch(struct mmsghdr* msgs, unsigned int count, int flags) noexcept {
    if (!is_valid() || !msgs || count == 0) return 0;
    return ::recvmmsg(fd_, msgs, count, flags, nullptr);
}
#endif

} // namespace aegon::net

