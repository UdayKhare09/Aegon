#pragma once

#include "core/engine/IoEngine.h"
#include "core/Task.h"
#include "core/OsSocket.h"
#include "net/SocketAddress.h"
#include "net/SocketOptions.h"
#include <span>
#include <optional>
#include <string_view>

#if defined(__linux__)
#include <sys/socket.h>
#include <netinet/udp.h>
#endif

namespace aegon::net {

/**
 * @brief High-performance non-blocking UDP datagram socket.
 * Supports dual-stack IPv4/IPv6, GSO (Generic Segmentation Offload),
 * GRO (Generic Receive Offload), sendmmsg, and recvmmsg.
 */
class UdpSocket {
public:
    UdpSocket() = default;
    explicit UdpSocket(core::os_socket_t fd, SocketAddress local_addr = {}) noexcept
        : fd_(fd), local_addr_(local_addr) {}

    ~UdpSocket() {
        close();
    }

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    UdpSocket(UdpSocket&& other) noexcept
        : fd_(other.fd_), local_addr_(other.local_addr_) {
        other.fd_ = core::INVALID_OS_SOCKET;
    }

    UdpSocket& operator=(UdpSocket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            local_addr_ = other.local_addr_;
            other.fd_ = core::INVALID_OS_SOCKET;
        }
        return *this;
    }

    /**
     * @brief Creates and binds UDP socket with optional SO_REUSEPORT and dual-stack.
     */
    [[nodiscard]] static std::optional<UdpSocket> bind(const SocketAddress& addr, bool reuse_port = true);
    [[nodiscard]] static std::optional<UdpSocket> bind(uint16_t port, std::string_view host = "0.0.0.0", bool reuse_port = true);

    [[nodiscard]] bool is_valid() const noexcept {
        return core::is_valid_socket(fd_);
    }

    [[nodiscard]] core::os_socket_t native_handle() const noexcept {
        return fd_;
    }

    [[nodiscard]] const SocketAddress& local_address() const noexcept {
        return local_addr_;
    }

    [[nodiscard]] core::os_socket_t release() noexcept {
        return std::exchange(fd_, core::INVALID_OS_SOCKET);
    }

    void close() noexcept {
        if (core::is_valid_socket(fd_)) {
            core::close_socket(fd_);
            fd_ = core::INVALID_OS_SOCKET;
        }
    }

    // -----------------------------------------------------------------------
    // Datagram I/O Operations
    // -----------------------------------------------------------------------

    /**
     * @brief Asynchronous single datagram receive via IoEngine.
     */
    [[nodiscard]] core::Task<int> recvfrom(core::IoEngine& engine, void* buf, size_t len, SocketAddress& from_addr);

    /**
     * @brief Asynchronous recvmsg via IoEngine.
     */
    [[nodiscard]] core::Task<int> recvmsg(core::IoEngine& engine, msghdr* msg) {
        co_return co_await engine.recvmsg(fd_, msg);
    }

    /**
     * @brief Asynchronous single datagram send via OS sendto.
     */
    [[nodiscard]] int sendto(const void* buf, size_t len, const SocketAddress& to_addr, int flags = 0) noexcept;

    /**
     * @brief Batched UDP transmit using Linux sendmmsg and optional UDP_SEGMENT (GSO).
     */
#if defined(__linux__)
    [[nodiscard]] int send_batch(struct mmsghdr* msgs, unsigned int count, int flags = 0) noexcept;
    [[nodiscard]] int recv_batch(struct mmsghdr* msgs, unsigned int count, int flags = MSG_DONTWAIT) noexcept;
#endif

    /**
     * @brief Configure kernel socket buffers.
     */
    bool set_buffer_sizes(int rcvbuf_bytes = 4 * 1024 * 1024, int sndbuf_bytes = 4 * 1024 * 1024) noexcept {
        bool ok = SocketOptions::set_recv_buffer_size(fd_, rcvbuf_bytes);
        ok = SocketOptions::set_send_buffer_size(fd_, sndbuf_bytes) && ok;
        return ok;
    }

private:
    core::os_socket_t fd_{core::INVALID_OS_SOCKET};
    SocketAddress local_addr_{};
};

} // namespace aegon::net

