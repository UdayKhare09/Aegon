#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <span>
#include <array>
#include <cstring>
#include <endian.h>
#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace aegon::http::websocket {

enum class Opcode : uint8_t {
    Continuation = 0x0,
    Text         = 0x1,
    Binary       = 0x2,
    Close        = 0x8,
    Ping         = 0x9,
    Pong         = 0xA
};

[[nodiscard]] constexpr bool is_control_opcode(Opcode op) noexcept {
    return (static_cast<uint8_t>(op) & 0x8) != 0;
}

enum class CloseCode : uint16_t {
    Normal              = 1000,
    GoingAway           = 1001,
    ProtocolError       = 1002,
    UnsupportedData     = 1003,
    NoStatusReceived    = 1005,
    AbnormalClosure     = 1006,
    InvalidFramePayload = 1007,
    PolicyViolation     = 1008,
    MessageTooBig       = 1009,
    MandatoryExtension  = 1010,
    InternalError       = 1011
};

struct FrameHeader {
    bool fin{true};
    bool rsv1{false};
    bool rsv2{false};
    bool rsv3{false};
    Opcode opcode{Opcode::Text};
    bool masked{false};
    uint32_t mask_key{0};
    uint64_t payload_len{0};
    size_t header_len{0};
};

enum class FrameParseResult {
    Complete,
    NeedMoreData,
    ProtocolError
};

/**
 * @brief Parses an RFC 6455 WebSocket frame header from a buffer.
 */
inline FrameParseResult parse_frame_header(std::string_view buf, FrameHeader& out) noexcept {
    if (buf.size() < 2) {
        return FrameParseResult::NeedMoreData;
    }

    const auto* p = reinterpret_cast<const uint8_t*>(buf.data());
    uint8_t b0 = p[0];
    uint8_t b1 = p[1];

    out.fin = (b0 & 0x80) != 0;
    out.rsv1 = (b0 & 0x40) != 0;
    out.rsv2 = (b0 & 0x20) != 0;
    out.rsv3 = (b0 & 0x10) != 0;
    out.opcode = static_cast<Opcode>(b0 & 0x0F);
    out.masked = (b1 & 0x80) != 0;

    // RFC 6455 §5.2: Control frames MUST NOT have RSV bits or fragment (FIN=1)
    if (is_control_opcode(out.opcode)) {
        if (!out.fin || out.rsv1 || out.rsv2 || out.rsv3) {
            return FrameParseResult::ProtocolError;
        }
    }

    uint8_t len_code = b1 & 0x7F;
    size_t offset = 2;

    if (len_code < 126) {
        out.payload_len = len_code;
    } else if (len_code == 126) {
        if (buf.size() < offset + 2) {
            return FrameParseResult::NeedMoreData;
        }
        uint16_t raw_len;
        std::memcpy(&raw_len, p + offset, 2);
        out.payload_len = be16toh(raw_len);
        offset += 2;
    } else { // 127
        if (buf.size() < offset + 8) {
            return FrameParseResult::NeedMoreData;
        }
        uint64_t raw_len;
        std::memcpy(&raw_len, p + offset, 8);
        out.payload_len = be64toh(raw_len);
        offset += 8;
    }

    // Control frames payload MUST be 125 bytes or less
    if (is_control_opcode(out.opcode) && out.payload_len > 125) {
        return FrameParseResult::ProtocolError;
    }

    if (out.masked) {
        if (buf.size() < offset + 4) {
            return FrameParseResult::NeedMoreData;
        }
        std::memcpy(&out.mask_key, p + offset, 4);
        offset += 4;
    } else {
        out.mask_key = 0;
    }

    out.header_len = offset;
    return FrameParseResult::Complete;
}

/**
 * @brief In-place AVX2/SIMD unmasking of client WebSocket payloads.
 */
inline void unmask_payload_inplace(uint8_t* data, size_t len, uint32_t mask_key) noexcept {
    if (len == 0 || mask_key == 0) return;

    size_t i = 0;
    const uint8_t* k = reinterpret_cast<const uint8_t*>(&mask_key);

#if defined(__AVX2__)
    if (len >= 32) {
        // Broadcast the 4-byte mask key across all 32 bytes of the __m256i register
        __m256i mask256 = _mm256_set1_epi32(static_cast<int>(mask_key));
        size_t simd_limit = len & ~size_t(31);
        for (; i < simd_limit; i += 32) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
            chunk = _mm256_xor_si256(chunk, mask256);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(data + i), chunk);
        }
    }
#endif

    // Process remaining bytes
    for (; i < len; ++i) {
        data[i] ^= k[i % 4];
    }
}

/**
 * @brief Constructs an unmasked server-to-client WebSocket frame header.
 * @return Number of header bytes written to out_buf.
 */
inline size_t serialize_frame_header(Opcode op, size_t payload_len, uint8_t* out_buf, bool fin = true) noexcept {
    uint8_t b0 = (fin ? 0x80 : 0x00) | (static_cast<uint8_t>(op) & 0x0F);
    out_buf[0] = b0;

    // Server-to-client frames MUST NOT be masked (RFC 6455 §5.1)
    if (payload_len < 126) {
        out_buf[1] = static_cast<uint8_t>(payload_len);
        return 2;
    } else if (payload_len <= 0xFFFF) {
        out_buf[1] = 126;
        uint16_t be_len = htobe16(static_cast<uint16_t>(payload_len));
        std::memcpy(out_buf + 2, &be_len, 2);
        return 4;
    } else {
        out_buf[1] = 127;
        uint64_t be_len = htobe64(static_cast<uint64_t>(payload_len));
        std::memcpy(out_buf + 2, &be_len, 8);
        return 10;
    }
}

} // namespace aegon::http::websocket
