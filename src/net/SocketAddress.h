#pragma once

#include "core/OsSocket.h"
#include <string>
#include <string_view>
#include <optional>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

namespace aegon::net {

/**
 * @brief High-performance, zero-allocation socket address wrapper.
 * Directly holds sockaddr_storage with zero heap allocation.
 */
class SocketAddress {
public:
    constexpr SocketAddress() noexcept {
        std::memset(&storage_, 0, sizeof(storage_));
        storage_.ss_family = AF_UNSPEC;
    }

    SocketAddress(const sockaddr* sa, socklen_t len) noexcept {
        assign(sa, len);
    }

    SocketAddress(const sockaddr_storage& ss, socklen_t len) noexcept {
        assign(reinterpret_cast<const sockaddr*>(&ss), len);
    }

    // Factory methods
    [[nodiscard]] static std::optional<SocketAddress> from_ipv4(std::string_view ip, uint16_t port) noexcept;
    [[nodiscard]] static std::optional<SocketAddress> from_ipv6(std::string_view ip, uint16_t port) noexcept;
    [[nodiscard]] static std::optional<SocketAddress> parse(std::string_view ip_or_host, uint16_t port) noexcept;
    [[nodiscard]] static SocketAddress any_ipv4(uint16_t port) noexcept;
    [[nodiscard]] static SocketAddress any_ipv6(uint16_t port) noexcept;

#if !defined(_WIN32)
    [[nodiscard]] static std::optional<SocketAddress> from_unix_domain(std::string_view path) noexcept;
#endif

    void assign(const sockaddr* sa, socklen_t len) noexcept {
        if (!sa || len == 0) {
            std::memset(&storage_, 0, sizeof(storage_));
            storage_.ss_family = AF_UNSPEC;
            len_ = sizeof(sockaddr_storage);
            return;
        }
        len_ = (len <= sizeof(storage_)) ? len : sizeof(storage_);
        std::memcpy(&storage_, sa, len_);
    }

    [[nodiscard]] sa_family_t family() const noexcept {
        return storage_.ss_family;
    }

    [[nodiscard]] bool is_ipv4() const noexcept { return storage_.ss_family == AF_INET; }
    [[nodiscard]] bool is_ipv6() const noexcept { return storage_.ss_family == AF_INET6; }
    [[nodiscard]] bool is_unix() const noexcept {
#if !defined(_WIN32)
        return storage_.ss_family == AF_UNIX;
#else
        return false;
#endif
    }
    [[nodiscard]] bool is_valid() const noexcept { return storage_.ss_family != AF_UNSPEC; }

    [[nodiscard]] uint16_t port() const noexcept;
    void set_port(uint16_t port) noexcept;

    [[nodiscard]] std::string ip() const;
    [[nodiscard]] std::string to_string() const;

    [[nodiscard]] const sockaddr* as_sockaddr() const noexcept {
        return reinterpret_cast<const sockaddr*>(&storage_);
    }

    [[nodiscard]] sockaddr* as_sockaddr() noexcept {
        return reinterpret_cast<sockaddr*>(&storage_);
    }

    [[nodiscard]] const sockaddr_in* as_ipv4() const noexcept {
        return reinterpret_cast<const sockaddr_in*>(&storage_);
    }

    [[nodiscard]] sockaddr_in* as_ipv4() noexcept {
        return reinterpret_cast<sockaddr_in*>(&storage_);
    }

    [[nodiscard]] const sockaddr_in6* as_ipv6() const noexcept {
        return reinterpret_cast<const sockaddr_in6*>(&storage_);
    }

    [[nodiscard]] sockaddr_in6* as_ipv6() noexcept {
        return reinterpret_cast<sockaddr_in6*>(&storage_);
    }

    [[nodiscard]] const sockaddr_storage& storage() const noexcept { return storage_; }
    [[nodiscard]] sockaddr_storage& storage() noexcept { return storage_; }

    [[nodiscard]] socklen_t length() const noexcept { return len_; }
    void set_length(socklen_t l) noexcept { len_ = l; }

    bool operator==(const SocketAddress& other) const noexcept;
    bool operator!=(const SocketAddress& other) const noexcept { return !(*this == other); }

private:
    sockaddr_storage storage_{};
    socklen_t len_{sizeof(sockaddr_storage)};
};

} // namespace aegon::net

