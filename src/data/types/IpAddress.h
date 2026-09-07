#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <string_view>
#include <optional>
#include <compare>
#include <ostream>
#include <istream>
#include <cstring>
#include <arpa/inet.h>

namespace aegon::data::types {

/**
 * @brief High-performance dual IPv4 and IPv6 network address.
 *
 * Implements zero-allocation string parsing and formatting, CIDR subnet matching,
 * and seamless Glaze JSON DTO integration. Matches SQL `INET` and MongoDB IP fields.
 */
class IpAddress {
public:
    std::array<uint8_t, 16> bytes_{};
    uint8_t version_{4}; // 4 or 6

    constexpr IpAddress() noexcept = default;

    constexpr explicit IpAddress(uint32_t ipv4_be) noexcept : version_(4) {
        bytes_[0] = static_cast<uint8_t>((ipv4_be >> 24) & 0xFF);
        bytes_[1] = static_cast<uint8_t>((ipv4_be >> 16) & 0xFF);
        bytes_[2] = static_cast<uint8_t>((ipv4_be >> 8) & 0xFF);
        bytes_[3] = static_cast<uint8_t>(ipv4_be & 0xFF);
    }

    constexpr explicit IpAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) noexcept : version_(4) {
        bytes_[0] = a; bytes_[1] = b; bytes_[2] = c; bytes_[3] = d;
    }

    constexpr explicit IpAddress(const std::array<uint8_t, 16>& ipv6_bytes) noexcept
        : bytes_(ipv6_bytes), version_(6) {}

    [[nodiscard]] constexpr bool is_ipv4() const noexcept { return version_ == 4; }
    [[nodiscard]] constexpr bool is_ipv6() const noexcept { return version_ == 6; }

    [[nodiscard]] constexpr const uint8_t* as_bytes() const noexcept { return bytes_.data(); }
    [[nodiscard]] constexpr size_t byte_count() const noexcept { return version_ == 4 ? 4 : 16; }

    [[nodiscard]] constexpr bool is_loopback() const noexcept {
        if (version_ == 4) {
            return bytes_[0] == 127;
        }
        for (size_t i = 0; i < 15; ++i) {
            if (bytes_[i] != 0) return false;
        }
        return bytes_[15] == 1;
    }

    [[nodiscard]] constexpr bool is_private() const noexcept {
        if (version_ == 4) {
            // 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16
            if (bytes_[0] == 10) return true;
            if (bytes_[0] == 172 && (bytes_[1] >= 16 && bytes_[1] <= 31)) return true;
            if (bytes_[0] == 192 && bytes_[1] == 168) return true;
            return false;
        }
        // IPv6 Unique Local Address: fc00::/7
        return (bytes_[0] & 0xFE) == 0xFC;
    }

    [[nodiscard]] std::string to_string() const {
        char buf[INET6_ADDRSTRLEN];
        int af = (version_ == 4) ? AF_INET : AF_INET6;
        if (inet_ntop(af, bytes_.data(), buf, sizeof(buf))) {
            return std::string(buf);
        }
        return version_ == 4 ? "0.0.0.0" : "::";
    }

    [[nodiscard]] static std::optional<IpAddress> from_string(std::string_view str) noexcept {
        if (str.empty()) return std::nullopt;

        // Null-terminated copy for inet_pton (short stack buffer)
        char buf[64];
        if (str.size() >= sizeof(buf)) return std::nullopt;
        std::memcpy(buf, str.data(), str.size());
        buf[str.size()] = '\0';

        // Try IPv4 first
        if (str.find(':') == std::string_view::npos) {
            struct in_addr addr4;
            if (inet_pton(AF_INET, buf, &addr4) == 1) {
                IpAddress ip;
                ip.version_ = 4;
                std::memcpy(ip.bytes_.data(), &addr4, 4);
                return ip;
            }
        } else {
            // Try IPv6
            struct in6_addr addr6;
            if (inet_pton(AF_INET6, buf, &addr6) == 1) {
                IpAddress ip;
                ip.version_ = 6;
                std::memcpy(ip.bytes_.data(), &addr6, 16);
                return ip;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Checks if this IP address falls within a given CIDR subnet (e.g. "192.168.1.0/24" or "2001:db8::/32").
     */
    [[nodiscard]] bool in_subnet(std::string_view cidr) const noexcept {
        size_t slash = cidr.find('/');
        if (slash == std::string_view::npos) {
            auto exact = from_string(cidr);
            return exact && (*exact == *this);
        }

        auto net = from_string(cidr.substr(0, slash));
        if (!net || net->version_ != version_) return false;

        std::string_view bits_str = cidr.substr(slash + 1);
        unsigned prefix_bits = 0;
        for (char c : bits_str) {
            if (c < '0' || c > '9') return false;
            prefix_bits = prefix_bits * 10 + (c - '0');
        }

        unsigned max_bits = version_ == 4 ? 32 : 128;
        if (prefix_bits > max_bits) return false;

        size_t full_bytes = prefix_bits / 8;
        size_t rem_bits = prefix_bits % 8;

        if (std::memcmp(bytes_.data(), net->bytes_.data(), full_bytes) != 0) {
            return false;
        }

        if (rem_bits > 0) {
            uint8_t mask = static_cast<uint8_t>(~((1 << (8 - rem_bits)) - 1));
            return (bytes_[full_bytes] & mask) == (net->bytes_[full_bytes] & mask);
        }

        return true;
    }

    [[nodiscard]] auto operator<=>(const IpAddress& other) const noexcept {
        if (auto cmp = version_ <=> other.version_; cmp != 0) return cmp;
        return bytes_ <=> other.bytes_;
    }

    [[nodiscard]] bool operator==(const IpAddress& other) const noexcept = default;
};

inline std::ostream& operator<<(std::ostream& os, const IpAddress& ip) {
    return os << ip.to_string();
}

inline std::istream& operator>>(std::istream& is, IpAddress& ip) {
    std::string s;
    if (is >> s) {
        if (auto parsed = IpAddress::from_string(s)) {
            ip = *parsed;
        } else {
            is.setstate(std::ios_base::failbit);
        }
    }
    return is;
}

} // namespace aegon::data::types

namespace aegon::data {
using types::IpAddress;
}

template <>
struct std::hash<aegon::data::types::IpAddress> {
    [[nodiscard]] size_t operator()(const aegon::data::types::IpAddress& ip) const noexcept {
        uint64_t w0 = 0, w1 = 0;
        std::memcpy(&w0, ip.bytes_.data(), 8);
        std::memcpy(&w1, ip.bytes_.data() + 8, 8);
        return std::hash<uint64_t>{}(w0 ^ (w1 + 0x9e3779b97f4a7c15ULL) ^ ip.version_);
    }
};

#if __has_include(<glaze/glaze.hpp>)
#include <glaze/glaze.hpp>

template <>
struct glz::meta<aegon::data::types::IpAddress> {
    static constexpr auto value = glz::custom<
        [](aegon::data::types::IpAddress& ip, const std::string_view s, glz::context& ctx) {
            auto parsed = aegon::data::types::IpAddress::from_string(s);
            if (!parsed) {
                ctx.error = glz::error_code::syntax_error;
                return;
            }
            ip = *parsed;
        },
        [](const aegon::data::types::IpAddress& ip) -> std::string {
            return ip.to_string();
        }
    >;
};
#endif
