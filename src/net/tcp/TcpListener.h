#pragma once

#include "core/engine/IoEngine.h"
#include "core/Task.h"
#include "core/OsSocket.h"
#include "net/SocketAddress.h"
#include "net/SocketOptions.h"
#include "net/tcp/TcpStream.h"
#include <string>
#include <string_view>
#include <optional>

namespace aegon::net {

struct TcpListenConfig {
    uint16_t port{0};
    std::string host{"0.0.0.0"};
    int backlog{32768};
    bool reuse_addr{true};
    bool reuse_port{true};
    bool tcp_nodelay{true};
    bool tcp_keepalive{false};
    int keepidle{60};
    int keepintvl{10};
    int keepcnt{3};
    bool defer_accept{false};
};

/**
 * @brief High-performance TCP server listener supporting multi-shot kernel accept and multi-threading via SO_REUSEPORT.
 */
class TcpListener {
public:
    TcpListener() = default;
    explicit TcpListener(core::os_socket_t fd, SocketAddress local_addr = {}) noexcept
        : fd_(fd), local_addr_(local_addr) {}

    ~TcpListener() {
        close();
    }

    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    TcpListener(TcpListener&& other) noexcept
        : fd_(other.fd_), local_addr_(other.local_addr_) {
        other.fd_ = core::INVALID_OS_SOCKET;
    }

    TcpListener& operator=(TcpListener&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            local_addr_ = other.local_addr_;
            other.fd_ = core::INVALID_OS_SOCKET;
        }
        return *this;
    }

    /**
     * @brief Creates, binds, tunes, and starts listening on the configured address.
     */
    [[nodiscard]] static std::optional<TcpListener> bind(const SocketAddress& addr, const TcpListenConfig& cfg = {});
    [[nodiscard]] static std::optional<TcpListener> bind(uint16_t port, std::string_view host = "0.0.0.0", const TcpListenConfig& cfg = {});

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

    /**
     * @brief Accepts a single incoming TCP connection asynchronously.
     */
    [[nodiscard]] core::Task<std::optional<TcpStream>> accept(core::IoEngine& engine);

    /**
     * @brief Spawns an io_uring multi-shot accept stream for continuous zero-allocation connection accepting.
     */
    [[nodiscard]] core::MultishotAcceptStream accept_multishot(core::IoEngine& engine) {
        return engine.accept_multishot(fd_);
    }

private:
    core::os_socket_t fd_{core::INVALID_OS_SOCKET};
    SocketAddress local_addr_{};
};

} // namespace aegon::net

