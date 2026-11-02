#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>

namespace aegon::core {

using os_socket_t = int;
inline constexpr os_socket_t INVALID_OS_SOCKET = -1;
using socklen_t = ::socklen_t;

inline bool is_valid_socket(os_socket_t s) noexcept {
    return s != INVALID_OS_SOCKET && s >= 0;
}

inline int poll_sockets(struct pollfd* fds, unsigned long nfds, int timeout) noexcept {
    return ::poll(fds, nfds, timeout);
}

inline void init_sockets() noexcept {}

inline int close_socket(os_socket_t s) noexcept {
    return ::close(s);
}

inline int last_socket_error() noexcept {
    return errno;
}

} // namespace aegon::core
