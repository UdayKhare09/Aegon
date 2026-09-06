#pragma once

#include <cstdint>
#include <string_view>
#include <span>

namespace aegon::http::v3 {

// HTTP/3 ALPN protocol identifier: "h3"
inline constexpr std::string_view ALPN_H3 = "h3";

enum class PacketType : uint8_t {
    Initial   = 0x00,
    ZeroRtt   = 0x01,
    Handshake = 0x02,
    Retry     = 0x03,
    Short1Rtt = 0x04
};

struct QuicHeader {
    bool is_long_header{false};
    PacketType type{PacketType::Short1Rtt};
    uint32_t version{0};
    std::span<const uint8_t> dest_conn_id{};
    std::span<const uint8_t> src_conn_id{};

    [[nodiscard]] static bool is_quic_packet(std::span<const uint8_t> datagram) noexcept {
        if (datagram.empty()) return false;
        // The fixed bit (0x40) must be 1 in all QUIC packets
        return (datagram[0] & 0x40) != 0;
    }
};

} // namespace aegon::http::v3
