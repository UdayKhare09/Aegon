#pragma once

#include "core/engine/IoEngine.h"
#include "core/Task.h"
#include "core/OsSocket.h"
#include "net/SocketAddress.h"
#include "net/SocketOptions.h"
#include <span>
#include <utility>

namespace aegon::net {

/**
 * @brief Zero-overhead asynchronous TCP connection stream.
 * Direct wrapper around core::IoEngine awaiters with zero virtual calls and zero heap allocations.
 */
class TcpStream {
public:
    TcpStream(core::IoEngine& engine, core::os_socket_t fd) noexcept
        : engine_(&engine), fd_(fd) {}

    ~TcpStream() = default;

    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;

    TcpStream(TcpStream&& other) noexcept
        : engine_(other.engine_), fd_(other.fd_) {
        other.fd_ = core::INVALID_OS_SOCKET;
    }

    TcpStream& operator=(TcpStream&& other) noexcept {
        if (this != &other) {
            engine_ = other.engine_;
            fd_ = other.fd_;
            other.fd_ = core::INVALID_OS_SOCKET;
        }
        return *this;
    }

    [[nodiscard]] bool is_valid() const noexcept {
        return core::is_valid_socket(fd_);
    }

    [[nodiscard]] core::os_socket_t native_handle() const noexcept {
        return fd_;
    }

    [[nodiscard]] core::IoEngine& engine() const noexcept {
        return *engine_;
    }

    [[nodiscard]] core::os_socket_t release() noexcept {
        return std::exchange(fd_, core::INVALID_OS_SOCKET);
    }

    // -----------------------------------------------------------------------
    // Core I/O Operations (Directly inlined to IoEngine awaiters)
    // -----------------------------------------------------------------------

    /**
     * @brief Asynchronous read into caller-provided buffer.
     */
    [[nodiscard]] core::Task<int> recv(void* buf, size_t len, int flags = 0) noexcept {
        co_return co_await engine_->recv(fd_, buf, len, flags);
    }

    /**
     * @brief Asynchronous zero-copy read from io_uring buffer ring group (bgid).
     */
    [[nodiscard]] core::Task<core::RecvResult> recv_provided(uint16_t bgid) noexcept {
        co_return co_await engine_->recv_provided(fd_, bgid);
    }

    /**
     * @brief Asynchronous send with standard kernel buffering.
     */
    [[nodiscard]] core::Task<int> send(const void* buf, size_t len) noexcept {
        co_return co_await engine_->send(fd_, buf, len);
    }

    /**
     * @brief Asynchronous send of all bytes in data span.
     */
    [[nodiscard]] core::Task<int> send_all(std::span<const uint8_t> data) noexcept {
        co_return co_await engine_->send_all(fd_, data);
    }

    /**
     * @brief Asynchronous zero-copy send via Linux IORING_OP_SEND_ZC.
     */
    [[nodiscard]] core::Task<int> send_zc(const void* buf, size_t len, int zc_flags = 0) noexcept {
        co_return co_await engine_->send_zc(
            fd_,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(buf), len),
            zc_flags);
    }

    [[nodiscard]] core::Task<int> send_zc(std::span<const uint8_t> data, int zc_flags = 0) noexcept {
        co_return co_await engine_->send_zc(fd_, data, zc_flags);
    }

    /**
     * @brief Zero-copy splice from a pipe read descriptor directly into this TCP socket.
     */
    [[nodiscard]] core::Task<int> splice_from(int pipe_read_fd, size_t bytes, unsigned int flags = 0) noexcept {
        co_return co_await engine_->splice(pipe_read_fd, -1, fd_, -1, static_cast<unsigned int>(bytes), flags);
    }

    /**
     * @brief Asynchronous socket shutdown (SHUT_RD, SHUT_WR, or SHUT_RDWR).
     */
    [[nodiscard]] core::Task<int> shutdown(int how = SHUT_WR) noexcept {
        if (!is_valid()) co_return 0;
        co_return co_await engine_->shutdown(fd_, how);
    }

    /**
     * @brief Asynchronous socket close through engine ring.
     */
    [[nodiscard]] core::Task<int> close() noexcept {
        auto sock = release();
        if (core::is_valid_socket(sock)) {
            co_return co_await engine_->close(sock);
        }
        co_return 0;
    }

    // -----------------------------------------------------------------------
    // Socket Options & Address Introspection
    // -----------------------------------------------------------------------

    bool set_nodelay(bool enable = true) noexcept {
        return SocketOptions::set_tcp_nodelay(fd_, enable);
    }

    bool set_keepalive(bool enable, int idle_sec = 60, int intvl_sec = 10, int count = 3) noexcept {
        return SocketOptions::set_tcp_keepalive(fd_, enable, idle_sec, intvl_sec, count);
    }

    [[nodiscard]] SocketAddress peer_address() const noexcept {
        SocketAddress addr;
        socklen_t len = sizeof(sockaddr_storage);
        if (::getpeername(fd_, addr.as_sockaddr(), &len) == 0) {
            addr.set_length(len);
        }
        return addr;
    }

    [[nodiscard]] SocketAddress local_address() const noexcept {
        SocketAddress addr;
        socklen_t len = sizeof(sockaddr_storage);
        if (::getsockname(fd_, addr.as_sockaddr(), &len) == 0) {
            addr.set_length(len);
        }
        return addr;
    }

private:
    core::IoEngine* engine_{nullptr};
    core::os_socket_t fd_{core::INVALID_OS_SOCKET};
};

} // namespace aegon::net
