#pragma once

#include "core/OsSocket.h"
#include <cstdint>

namespace aegon::net {

/**
 * @brief Zero-overhead utility for cross-platform socket tuning and option configuration.
 */
class SocketOptions {
public:
    static bool set_nonblocking(core::os_socket_t fd, bool nonblocking = true) noexcept;
    static bool set_reuse_address(core::os_socket_t fd, bool reuse = true) noexcept;
    static bool set_reuse_port(core::os_socket_t fd, bool reuse = true) noexcept;
    static bool set_tcp_nodelay(core::os_socket_t fd, bool nodelay = true) noexcept;
    static bool set_tcp_keepalive(core::os_socket_t fd, bool enable, int idle_sec = 60, int intvl_sec = 10, int count = 3) noexcept;
    static bool set_send_buffer_size(core::os_socket_t fd, int size_bytes) noexcept;
    static bool set_recv_buffer_size(core::os_socket_t fd, int size_bytes) noexcept;
    static bool set_ipv6_only(core::os_socket_t fd, bool v6only) noexcept;
    static bool set_tcp_defer_accept(core::os_socket_t fd, int timeout_sec = 1) noexcept;
    static bool set_tcp_quickack(core::os_socket_t fd, bool enable = true) noexcept;
};

} // namespace aegon::net

