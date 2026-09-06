#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <string>
#include <string_view>
#include <optional>
#include <compare>
#include <bit>
#include <cstring>
#include <ostream>
#include <immintrin.h>

namespace aegon::data {

/**
 * @brief High-performance, 16-byte aligned UUID structure supporting v4 and v7 (RFC 9562 / RFC 4122).
 *
 * Implements ultra-fast SIMD-accelerated serialization, parsing, and hardware-accelerated comparison/hashing.
 */
struct alignas(16) UUID {
    std::array<uint8_t, 16> data{};

    // Constructors
    constexpr UUID() noexcept = default;

    constexpr explicit UUID(const std::array<uint8_t, 16>& bytes) noexcept
        : data(bytes) {}

    constexpr explicit UUID(const uint8_t* bytes) noexcept {
        for (size_t i = 0; i < 16; ++i) {
            data[i] = bytes[i];
        }
    }

    // Nil UUID (all zeros)
    [[nodiscard]] static constexpr UUID nil() noexcept {
        return UUID{};
    }

    // Check if Nil
    [[nodiscard]] bool is_nil() const noexcept {
        const auto* w = reinterpret_cast<const uint64_t*>(data.data());
        return (w[0] == 0) && (w[1] == 0);
    }

    // Version accessor (e.g. 4 for v4, 7 for v7)
    [[nodiscard]] constexpr uint8_t version() const noexcept {
        return static_cast<uint8_t>((data[6] >> 4) & 0x0F);
    }

    // Variant accessor (1 for RFC 4122 / RFC 9562 standard variant)
    [[nodiscard]] constexpr uint8_t variant() const noexcept {
        uint8_t v = data[8];
        if ((v & 0x80) == 0x00) return 0; // NCS backward compatibility
        if ((v & 0xC0) == 0x80) return 1; // RFC 4122 / RFC 9562
        if ((v & 0xE0) == 0xC0) return 2; // Microsoft Corporation GUID
        return 3;                         // Reserved for future definition
    }

    /**
     * @brief Extracts the 48-bit Unix millisecond timestamp (valid for UUID v7).
     * Uses MOVBE / BSWAP for instant 1-instruction big-endian decoding.
     */
    [[nodiscard]] inline uint64_t timestamp_ms() const noexcept {
        uint64_t hi;
        std::memcpy(&hi, data.data(), sizeof(uint64_t));
        // Byte-swap to big-endian order, then shift right by 16 bits (48-bit timestamp)
        return std::byteswap(hi) >> 16;
    }

    // Raw bytes access
    [[nodiscard]] constexpr const uint8_t* as_bytes() const noexcept {
        return data.data();
    }

    [[nodiscard]] constexpr uint8_t* as_bytes() noexcept {
        return data.data();
    }

    // Hardware-accelerated 128-bit comparison matching canonical lexicographical string order
    [[nodiscard]] inline bool operator==(const UUID& other) const noexcept {
        const auto* a = reinterpret_cast<const uint64_t*>(data.data());
        const auto* b = reinterpret_cast<const uint64_t*>(other.data.data());
        return (a[0] == b[0]) && (a[1] == b[1]);
    }

    [[nodiscard]] inline std::strong_ordering operator<=>(const UUID& other) const noexcept {
        const auto* a = reinterpret_cast<const uint64_t*>(data.data());
        const auto* b = reinterpret_cast<const uint64_t*>(other.data.data());
        
        // Use byteswap so the comparison strictly reflects big-endian lexicographical order
        uint64_t a_hi = std::byteswap(a[0]);
        uint64_t b_hi = std::byteswap(b[0]);
        if (auto cmp = a_hi <=> b_hi; cmp != 0) {
            return cmp;
        }

        uint64_t a_lo = std::byteswap(a[1]);
        uint64_t b_lo = std::byteswap(b[1]);
        return a_lo <=> b_lo;
    }

    /**
     * @brief Formats the UUID into a canonical 36-character string into the destination buffer.
     * Guaranteed zero allocations. Output will be exactly 36 bytes (does not append null-terminator).
     * Accelerated via AVX-512 / SSSE3 byte shuffles.
     */
    void to_chars(char* out) const noexcept;

    /**
     * @brief Canonical 36-character string representation (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx).
     */
    [[nodiscard]] std::string to_string() const;

    /**
     * @brief Parses a canonical 36-character UUID string.
     * Validates hyphens and hex characters using SIMD.
     */
    [[nodiscard]] static std::optional<UUID> from_string(std::string_view str) noexcept;
};

// Stream operator
inline std::ostream& operator<<(std::ostream& os, const UUID& uuid) {
    return os << uuid.to_string();
}

} // namespace aegon::data

// Hardware-accelerated std::hash specialization using SSE4.2 CRC32 instructions
template <>
struct std::hash<aegon::data::UUID> {
    [[nodiscard]] size_t operator()(const aegon::data::UUID& uuid) const noexcept {
        const auto* ptr = reinterpret_cast<const uint64_t*>(uuid.data.data());
#if defined(__SSE4_2__)
        uint64_t h = _mm_crc32_u64(0x429562UL, ptr[0]);
        return static_cast<size_t>(_mm_crc32_u64(h, ptr[1]));
#else
        // Fallback 64-bit hash
        uint64_t h = ptr[0] ^ (ptr[1] + 0x9e3779b97f4a7c15ULL + (ptr[0] << 6) + (ptr[0] >> 2));
        return static_cast<size_t>(h);
#endif
    }
};
