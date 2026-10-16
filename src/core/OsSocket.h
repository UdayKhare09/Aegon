#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <io.h>
#include <BaseTsd.h>

#ifdef DELETE
#undef DELETE
#endif

using ssize_t = SSIZE_T;

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

// POSIX shutdown mappings
#ifndef SHUT_RD
#define SHUT_RD SD_RECEIVE
#endif
#ifndef SHUT_WR
#define SHUT_WR SD_SEND
#endif
#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// POSIX endian mappings using Winsock2 functions
#ifndef htobe16
#define htobe16(x) htons(x)
#endif
#ifndef be16toh
#define be16toh(x) ntohs(x)
#endif
#ifndef htobe32
#define htobe32(x) htonl(x)
#endif
#ifndef be32toh
#define be32toh(x) ntohl(x)
#endif
#ifndef htobe64
#define htobe64(x) htonll(x)
#endif
#ifndef be64toh
#define be64toh(x) ntohll(x)
#endif

namespace aegon::core {

using os_socket_t = SOCKET;
inline constexpr os_socket_t INVALID_OS_SOCKET = INVALID_SOCKET;
using socklen_t = int;

inline bool is_valid_socket(os_socket_t s) noexcept {
    return s != INVALID_OS_SOCKET;
}

inline int poll_sockets(struct pollfd* fds, unsigned long nfds, int timeout) noexcept {
    return ::WSAPoll(fds, nfds, timeout);
}

} // namespace aegon::core

#ifndef _IOVEC_DEFINED
#define _IOVEC_DEFINED
struct iovec {
    void*  iov_base;
    size_t iov_len;
};
#endif

#ifndef _MSGHDR_DEFINED
#define _MSGHDR_DEFINED
struct msghdr {
    void*         msg_name;
    int           msg_namelen;
    struct iovec* msg_iov;
    size_t        msg_iovlen;
    void*         msg_control;
    size_t        msg_controllen;
    int           msg_flags;
};
#endif

namespace aegon::core {

/**
 * @brief Initialize OS socket subsystem (WSAStartup on Windows).
 */
inline void init_sockets() noexcept {
    static struct WinsockInitializer {
        WinsockInitializer() noexcept {
            WSADATA wsa{};
            (void)::WSAStartup(MAKEWORD(2, 2), &wsa);
        }
        ~WinsockInitializer() noexcept {
            (void)::WSACleanup();
        }
    } s_init;
}

/**
 * @brief Close an OS socket descriptor cleanly.
 */
inline int close_socket(os_socket_t s) noexcept {
    return ::closesocket(s);
}

/**
 * @brief Get the last socket error code.
 */
inline int last_socket_error() noexcept {
    return ::WSAGetLastError();
}

} // namespace aegon::core

#else // Non-Windows (Linux, macOS, BSD)

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

#endif
