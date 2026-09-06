#pragma once

#include <cstdint>
#include <string_view>
#include <array>
#include <cstring>
#include <arpa/inet.h>

namespace aegon::http::v2 {

// HTTP/2 Connection Preface (24 bytes): "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
inline constexpr std::string_view CLIENT_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

enum class FrameType : uint8_t {
    DATA          = 0x0,
    HEADERS       = 0x1,
    PRIORITY      = 0x2,
    RST_STREAM    = 0x3,
    SETTINGS      = 0x4,
    PUSH_PROMISE  = 0x5,
    PING          = 0x6,
    GOAWAY        = 0x7,
    WINDOW_UPDATE = 0x8,
    CONTINUATION  = 0x9
};

namespace Flags {
    inline constexpr uint8_t NONE        = 0x0;
    inline constexpr uint8_t END_STREAM  = 0x1;
    inline constexpr uint8_t ACK         = 0x1; // For SETTINGS / PING
    inline constexpr uint8_t END_HEADERS = 0x4;
    inline constexpr uint8_t PADDED      = 0x8;
    inline constexpr uint8_t PRIORITY    = 0x20;
}

namespace SettingsId {
    inline constexpr uint16_t HEADER_TABLE_SIZE      = 0x1;
    inline constexpr uint16_t ENABLE_PUSH            = 0x2;
    inline constexpr uint16_t MAX_CONCURRENT_STREAMS = 0x3;
    inline constexpr uint16_t INITIAL_WINDOW_SIZE    = 0x4;
    inline constexpr uint16_t MAX_FRAME_SIZE         = 0x5;
    inline constexpr uint16_t MAX_HEADER_LIST_SIZE   = 0x6;
}

#pragma pack(push, 1)
struct RawFrameHeader {
    uint8_t length[3];
    uint8_t type;
    uint8_t flags;
    uint32_t stream_id; // in network byte order; highest bit reserved
};
#pragma pack(pop)

static_assert(sizeof(RawFrameHeader) == 9, "HTTP/2 frame header must be exactly 9 bytes");

struct FrameHeader {
    uint32_t length{0};
    FrameType type{FrameType::DATA};
    uint8_t flags{0};
    uint32_t stream_id{0};

    [[nodiscard]] static FrameHeader decode(const uint8_t* data) noexcept {
        FrameHeader hdr;
        hdr.length = (static_cast<uint32_t>(data[0]) << 16) |
                     (static_cast<uint32_t>(data[1]) << 8) |
                     (static_cast<uint32_t>(data[2]));
        hdr.type = static_cast<FrameType>(data[3]);
        hdr.flags = data[4];
        uint32_t raw_sid;
        std::memcpy(&raw_sid, data + 5, 4);
        hdr.stream_id = ntohl(raw_sid) & 0x7FFFFFFF;
        return hdr;
    }

    void encode(uint8_t* out) const noexcept {
        out[0] = static_cast<uint8_t>((length >> 16) & 0xFF);
        out[1] = static_cast<uint8_t>((length >> 8) & 0xFF);
        out[2] = static_cast<uint8_t>(length & 0xFF);
        out[3] = static_cast<uint8_t>(type);
        out[4] = flags;
        uint32_t net_sid = htonl(stream_id & 0x7FFFFFFF);
        std::memcpy(out + 5, &net_sid, 4);
    }
};

} // namespace aegon::http::v2
