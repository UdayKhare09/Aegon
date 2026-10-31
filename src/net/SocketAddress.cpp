#include "net/SocketAddress.h"
#include <cstdio>
#include <array>

namespace aegon::net {

std::optional<SocketAddress> SocketAddress::from_ipv4(std::string_view ip, uint16_t port) noexcept {
    SocketAddress addr;
    auto* sin = addr.as_ipv4();
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    addr.len_ = sizeof(sockaddr_in);

    std::string ip_str(ip);
    if (::inet_pton(AF_INET, ip_str.c_str(), &sin->sin_addr) <= 0) {
        return std::nullopt;
    }
    return addr;
}

std::optional<SocketAddress> SocketAddress::from_ipv6(std::string_view ip, uint16_t port) noexcept {
    SocketAddress addr;
    auto* sin6 = addr.as_ipv6();
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port = htons(port);
    addr.len_ = sizeof(sockaddr_in6);

    std::string ip_str(ip);
    if (::inet_pton(AF_INET6, ip_str.c_str(), &sin6->sin6_addr) <= 0) {
        return std::nullopt;
    }
    return addr;
}

std::optional<SocketAddress> SocketAddress::parse(std::string_view ip_or_host, uint16_t port) noexcept {
    if (auto v4 = from_ipv4(ip_or_host, port)) {
        return v4;
    }
    if (auto v6 = from_ipv6(ip_or_host, port)) {
        return v6;
    }
    return std::nullopt;
}

SocketAddress SocketAddress::any_ipv4(uint16_t port) noexcept {
    SocketAddress addr;
    auto* sin = addr.as_ipv4();
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    sin->sin_addr.s_addr = htonl(INADDR_ANY);
    addr.len_ = sizeof(sockaddr_in);
    return addr;
}

SocketAddress SocketAddress::any_ipv6(uint16_t port) noexcept {
    SocketAddress addr;
    auto* sin6 = addr.as_ipv6();
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port = htons(port);
    sin6->sin6_addr = in6addr_any;
    addr.len_ = sizeof(sockaddr_in6);
    return addr;
}

#if !defined(_WIN32)
std::optional<SocketAddress> SocketAddress::from_unix_domain(std::string_view path) noexcept {
    if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) {
        return std::nullopt;
    }
    SocketAddress addr;
    auto* sun = reinterpret_cast<sockaddr_un*>(&addr.storage_);
    sun->sun_family = AF_UNIX;
    std::memcpy(sun->sun_path, path.data(), path.size());
    sun->sun_path[path.size()] = '\0';
    addr.len_ = static_cast<socklen_t>(sizeof(sun->sun_family) + path.size() + 1);
    return addr;
}
#endif

uint16_t SocketAddress::port() const noexcept {
    if (is_ipv4()) {
        return ntohs(as_ipv4()->sin_port);
    }
    if (is_ipv6()) {
        return ntohs(as_ipv6()->sin6_port);
    }
    return 0;
}

void SocketAddress::set_port(uint16_t port) noexcept {
    if (is_ipv4()) {
        as_ipv4()->sin_port = htons(port);
    } else if (is_ipv6()) {
        as_ipv6()->sin6_port = htons(port);
    }
}

std::string SocketAddress::ip() const {
    char buf[INET6_ADDRSTRLEN] = {0};
    if (is_ipv4()) {
        if (::inet_ntop(AF_INET, &as_ipv4()->sin_addr, buf, sizeof(buf))) {
            return std::string(buf);
        }
    } else if (is_ipv6()) {
        if (::inet_ntop(AF_INET6, &as_ipv6()->sin6_addr, buf, sizeof(buf))) {
            return std::string(buf);
        }
    }
#if !defined(_WIN32)
    else if (is_unix()) {
        const auto* sun = reinterpret_cast<const sockaddr_un*>(&storage_);
        return std::string(sun->sun_path);
    }
#endif
    return {};
}

std::string SocketAddress::to_string() const {
    if (is_ipv4()) {
        return ip() + ":" + std::to_string(port());
    }
    if (is_ipv6()) {
        return "[" + ip() + "]:" + std::to_string(port());
    }
#if !defined(_WIN32)
    if (is_unix()) {
        return ip();
    }
#endif
    return "unspecified";
}

bool SocketAddress::operator==(const SocketAddress& other) const noexcept {
    if (storage_.ss_family != other.storage_.ss_family) return false;
    if (is_ipv4()) {
        return as_ipv4()->sin_port == other.as_ipv4()->sin_port &&
               as_ipv4()->sin_addr.s_addr == other.as_ipv4()->sin_addr.s_addr;
    }
    if (is_ipv6()) {
        return as_ipv6()->sin6_port == other.as_ipv6()->sin6_port &&
               std::memcmp(&as_ipv6()->sin6_addr, &other.as_ipv6()->sin6_addr, sizeof(in6_addr)) == 0;
    }
#if !defined(_WIN32)
    if (is_unix()) {
        const auto* sun1 = reinterpret_cast<const sockaddr_un*>(&storage_);
        const auto* sun2 = reinterpret_cast<const sockaddr_un*>(&other.storage_);
        return std::strcmp(sun1->sun_path, sun2->sun_path) == 0;
    }
#endif
    return false;
}

} // namespace aegon::net

