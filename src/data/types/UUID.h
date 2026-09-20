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
#include <istream>

#include "data/types/Hex.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace aegon::data::types {

namespace detail {

#if defined(__AVX512VBMI__) && defined(__AVX512F__)
// AVX-512 VBMI byte permutation table to insert hyphens into 36-char UUID string
alignas(64) inline constexpr uint8_t VBMI_FORMAT_PERMUTE[64] = {
    0, 1, 2, 3, 4, 5, 6, 7,      // 8 chars: time_low
    32,                          // '-' (at index 8)
    8, 9, 10, 11,                // 4 chars: time_mid
    32,                          // '-' (at index 13)
    12, 13, 14, 15,              // 4 chars: time_hi_and_version
    32,                          // '-' (at index 18)
    16, 17, 18, 19,              // 4 chars: clock_seq
    32,                          // '-' (at index 23)
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, // 12 chars: node
    // Padding
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// AVX-512 VBMI table to extract 32 hex characters from 36-char string (stripping hyphens)
alignas(64) inline constexpr uint8_t VBMI_PARSE_PERMUTE[64] = {
    0, 1, 2, 3, 4, 5, 6, 7,      // 0..7
    9, 10, 11, 12,               // 8..11 (skip index 8 '-')
    14, 15, 16, 17,              // 12..15 (skip index 13 '-')
    19, 20, 21, 22,              // 16..19 (skip index 18 '-')
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, // 20..31 (skip index 23 '-')
    // Padding
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
#endif

} // namespace detail

/**
 * @brief High-performance, 16-byte aligned UUID structure supporting v4 and v7 (RFC 9562 / RFC 4122).
 *
 * Implements ultra-fast SIMD-accelerated serialization, parsing, and hardware-accelerated comparison/hashing.
 * 100% Header-only.
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

    constexpr UUID(uint64_t hi, uint64_t lo) noexcept {
        std::memcpy(data.data(), &hi, sizeof(uint64_t));
        std::memcpy(data.data() + 8, &lo, sizeof(uint64_t));
    }

    // Parsing constructor
    explicit inline UUID(std::string_view str);

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
        return std::byteswap(hi) >> 16;
    }

    // Raw bytes access
    [[nodiscard]] constexpr const uint8_t* as_bytes() const noexcept {
        return data.data();
    }

    [[nodiscard]] constexpr uint8_t* as_bytes() noexcept {
        return data.data();
    }

    // Raw byte serialization
    [[nodiscard]] inline std::string bytes() const {
        return std::string(reinterpret_cast<const char*>(data.data()), 16);
    }

    inline void bytes(std::string& out) const {
        out.assign(reinterpret_cast<const char*>(data.data()), 16);
    }

    inline void bytes(char* out) const noexcept {
        std::memcpy(out, data.data(), 16);
    }

    // Comparison operators (canonical big-endian lexicographical order)
    [[nodiscard]] inline bool operator==(const UUID& other) const noexcept {
        const auto* a = reinterpret_cast<const uint64_t*>(data.data());
        const auto* b = reinterpret_cast<const uint64_t*>(other.data.data());
        return (a[0] == b[0]) && (a[1] == b[1]);
    }

    [[nodiscard]] inline std::strong_ordering operator<=>(const UUID& other) const noexcept {
        const auto* a = reinterpret_cast<const uint64_t*>(data.data());
        const auto* b = reinterpret_cast<const uint64_t*>(other.data.data());
        
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
     * Guaranteed zero allocations. Output is exactly 36 bytes (no null-terminator appended).
     * Fully inlined: accelerated via AVX-512 VBMI / SSSE3 byte shuffles with register hoisting.
     */
    inline void to_chars(char* out) const noexcept {
#if defined(__AVX512VBMI__) && defined(__AVX512F__) && defined(__AVX512BW__)
        __m128i raw = _mm_load_si128(reinterpret_cast<const __m128i*>(data.data()));

        __m128i mask_0f = _mm_set1_epi8(0x0F);
        __m128i hi_nibbles = _mm_and_si128(_mm_srli_epi16(raw, 4), mask_0f);
        __m128i lo_nibbles = _mm_and_si128(raw, mask_0f);

        __m128i nibbles_lo = _mm_unpacklo_epi8(hi_nibbles, lo_nibbles);
        __m128i nibbles_hi = _mm_unpackhi_epi8(hi_nibbles, lo_nibbles);

        __m128i hex_lut = _mm_load_si128(reinterpret_cast<const __m128i*>(detail::HEX_DIGITS_LOWER));
        __m128i ascii_lo = _mm_shuffle_epi8(hex_lut, nibbles_lo);
        __m128i ascii_hi = _mm_shuffle_epi8(hex_lut, nibbles_hi);

        __m256i ascii_256 = _mm256_set_m128i(ascii_hi, ascii_lo);
        __m512i ascii_512 = _mm512_castsi256_si512(ascii_256);
        ascii_512 = _mm512_mask_set1_epi8(ascii_512, 1ULL << 32, '-');

        __m512i permute_idx = _mm512_load_si512(reinterpret_cast<const __m512i*>(detail::VBMI_FORMAT_PERMUTE));
        __m512i formatted = _mm512_permutexvar_epi8(permute_idx, ascii_512);

        const uint64_t mask_36 = (1ULL << 36) - 1;
        _mm512_mask_storeu_epi8(out, mask_36, formatted);

#elif defined(__SSSE3__)
        __m128i raw = _mm_load_si128(reinterpret_cast<const __m128i*>(data.data()));
        __m128i mask_0f = _mm_set1_epi8(0x0F);
        __m128i hi = _mm_and_si128(_mm_srli_epi16(raw, 4), mask_0f);
        __m128i lo = _mm_and_si128(raw, mask_0f);

        __m128i nibbles_0 = _mm_unpacklo_epi8(hi, lo);
        __m128i nibbles_1 = _mm_unpackhi_epi8(hi, lo);

        __m128i hex_lut = _mm_load_si128(reinterpret_cast<const __m128i*>(detail::HEX_DIGITS_LOWER));
        __m128i ascii_0 = _mm_shuffle_epi8(hex_lut, nibbles_0);
        __m128i ascii_1 = _mm_shuffle_epi8(hex_lut, nibbles_1);

        _mm_storeu_si64(out, ascii_0); // chars 0..7
        out[8] = '-';

        uint32_t c8_11 = _mm_cvtsi128_si32(_mm_srli_si128(ascii_0, 8));
        std::memcpy(out + 9, &c8_11, 4);
        out[13] = '-';

        uint32_t c12_15 = _mm_cvtsi128_si32(_mm_srli_si128(ascii_0, 12));
        std::memcpy(out + 14, &c12_15, 4);
        out[18] = '-';

        uint32_t c16_19 = _mm_cvtsi128_si32(ascii_1);
        std::memcpy(out + 19, &c16_19, 4);
        out[23] = '-';

        _mm_storeu_si64(out + 24, _mm_srli_si128(ascii_1, 4));
        uint32_t c28_31 = _mm_cvtsi128_si32(_mm_srli_si128(ascii_1, 12));
        std::memcpy(out + 32, &c28_31, 4);

#else
        size_t out_idx = 0;
        for (size_t i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) {
                out[out_idx++] = '-';
            }
            uint8_t b = data[i];
            out[out_idx++] = detail::HEX_DIGITS_LOWER[b >> 4];
            out[out_idx++] = detail::HEX_DIGITS_LOWER[b & 0x0F];
        }
#endif
    }

    /**
     * @brief Canonical 36-character string representation (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx).
     */
    [[nodiscard]] inline std::string to_string() const {
        std::string s(36, '\0');
        to_chars(s.data());
        return s;
    }

    // Convenience & crashoz compatibility string aliases
    [[nodiscard]] inline std::string str() const {
        return to_string();
    }

    inline void str(char* out) const noexcept {
        to_chars(out);
    }

    inline void str(std::string& s) const {
        s.resize(36);
        to_chars(s.data());
    }

    /**
     * @brief Fast in-place parsing without allocating string_view.
     * Validates hyphens and hex characters using SIMD where available.
     */
    [[nodiscard]] static inline bool from_chars(const char* s, UUID& out) noexcept {
        // Validate hyphens at canonical positions
        if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') [[unlikely]] {
            return false;
        }

#if defined(__AVX512VBMI__) && defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
        // -------------------------------------------------------------
        // Tier 1: AVX-512 VBMI (Full SIMD verification + decode)
        // -------------------------------------------------------------
        const uint64_t mask_36 = (1ULL << 36) - 1;
        __m512i chars_512 = _mm512_maskz_loadu_epi8(mask_36, s);

        __m512i parse_permute = _mm512_load_si512(reinterpret_cast<const __m512i*>(detail::VBMI_PARSE_PERMUTE));
        __m512i hex_compact = _mm512_permutexvar_epi8(parse_permute, chars_512);
        __m256i hex_chars = _mm512_castsi512_si256(hex_compact);

        __m256i c_0 = _mm256_set1_epi8('0');
        __m256i c_9 = _mm256_set1_epi8('9');
        __m256i c_a = _mm256_set1_epi8('a');
        __m256i c_f = _mm256_set1_epi8('f');
        __m256i c_upper_A = _mm256_set1_epi8('A');
        __m256i c_upper_Z = _mm256_set1_epi8('Z');

        __m256i is_digit = _mm256_and_si256(
            _mm256_cmpgt_epi8(hex_chars, _mm256_sub_epi8(c_0, _mm256_set1_epi8(1))),
            _mm256_cmpgt_epi8(_mm256_add_epi8(c_9, _mm256_set1_epi8(1)), hex_chars)
        );

        __m256i is_upper = _mm256_and_si256(
            _mm256_cmpgt_epi8(hex_chars, _mm256_sub_epi8(c_upper_A, _mm256_set1_epi8(1))),
            _mm256_cmpgt_epi8(_mm256_add_epi8(c_upper_Z, _mm256_set1_epi8(1)), hex_chars)
        );
        __m256i hex_lower = _mm256_or_si256(hex_chars, _mm256_and_si256(is_upper, _mm256_set1_epi8(0x20)));

        __m256i is_alpha = _mm256_and_si256(
            _mm256_cmpgt_epi8(hex_lower, _mm256_sub_epi8(c_a, _mm256_set1_epi8(1))),
            _mm256_cmpgt_epi8(_mm256_add_epi8(c_f, _mm256_set1_epi8(1)), hex_lower)
        );

        __m256i is_valid = _mm256_or_si256(is_digit, is_alpha);
        if (_mm256_movemask_epi8(is_valid) != static_cast<int>(0xFFFFFFFF)) [[unlikely]] {
            return false;
        }

        __m256i digit_val = _mm256_sub_epi8(hex_lower, c_0);
        __m256i alpha_val = _mm256_add_epi8(_mm256_sub_epi8(hex_lower, c_a), _mm256_set1_epi8(10));
        __m256i nibbles_256 = _mm256_blendv_epi8(alpha_val, digit_val, is_digit);

        __m256i mult = _mm256_set1_epi16(0x0110);
        __m256i packed_16bit = _mm256_maddubs_epi16(nibbles_256, mult);

        __m128i half0 = _mm256_castsi256_si128(packed_16bit);
        __m128i half1 = _mm256_extracti128_si256(packed_16bit, 1);
        __m128i bytes_16 = _mm_packus_epi16(half0, half1);

        _mm_store_si128(reinterpret_cast<__m128i*>(out.data.data()), bytes_16);
        return true;

#else
        // -------------------------------------------------------------
        // Tier 2 & 3: Ultra-Fast Branchless Table-Driven Portable Fallback
        // -------------------------------------------------------------
        size_t s_idx = 0;
        uint32_t error_acc = 0;

        for (size_t b_idx = 0; b_idx < 16; ++b_idx) {
            if (s_idx == 8 || s_idx == 13 || s_idx == 18 || s_idx == 23) {
                s_idx++;
            }
            uint8_t hi = detail::HEX_DECODE_TABLE[static_cast<uint8_t>(s[s_idx++])];
            uint8_t lo = detail::HEX_DECODE_TABLE[static_cast<uint8_t>(s[s_idx++])];

            error_acc |= (hi | lo);
            out.data[b_idx] = static_cast<uint8_t>((hi << 4) | lo);
        }

        return (error_acc & 0xF0) == 0;
#endif
    }

    /**
     * @brief Parses a canonical 36-character UUID string.
     */
    [[nodiscard]] static inline std::optional<UUID> from_string(std::string_view str) noexcept {
        if (str.size() != 36) [[unlikely]] {
            return std::nullopt;
        }
        UUID result;
        if (from_chars(str.data(), result)) [[likely]] {
            return result;
        }
        return std::nullopt;
    }

    // crashoz compatibility factory method
    [[nodiscard]] static inline UUID fromStrFactory(std::string_view str) noexcept {
        return from_string(str).value_or(nil());
    }
};

inline UUID::UUID(std::string_view str) {
    if (auto parsed = from_string(str)) {
        *this = *parsed;
    } else {
        *this = nil();
    }
}

// Stream operators
inline std::ostream& operator<<(std::ostream& os, const UUID& uuid) {
    char buf[36];
    uuid.to_chars(buf);
    return os.write(buf, 36);
}

inline std::istream& operator>>(std::istream& is, UUID& uuid) {
    std::string s;
    if (is >> s) {
        if (auto parsed = UUID::from_string(s)) {
            uuid = *parsed;
        } else {
            is.setstate(std::ios_base::failbit);
        }
    }
    return is;
}

} // namespace aegon::data::types

namespace aegon::data {
using types::UUID;
} // namespace aegon::data

// Hardware-accelerated std::hash specialization using SSE4.2 CRC32 instructions
template <>
struct std::hash<aegon::data::types::UUID> {
    [[nodiscard]] size_t operator()(const aegon::data::types::UUID& uuid) const noexcept {
        const auto* ptr = reinterpret_cast<const uint64_t*>(uuid.data.data());
#if defined(__SSE4_2__)
        uint64_t h = _mm_crc32_u64(0x429562UL, ptr[0]);
        return static_cast<size_t>(_mm_crc32_u64(h, ptr[1]));
#else
        uint64_t h = ptr[0] ^ (ptr[1] + 0x9e3779b97f4a7c15ULL + (ptr[0] << 6) + (ptr[0] >> 2));
        return static_cast<size_t>(h);
#endif
    }
};

#if __has_include(<glaze/glaze.hpp>)
#include <glaze/glaze.hpp>

template <>
struct glz::meta<aegon::data::types::UUID> {
    static constexpr auto value = glz::custom<
        [](aegon::data::types::UUID& u, const std::string_view s, glz::context& ctx) {
            auto parsed = aegon::data::types::UUID::from_string(s);
            if (!parsed) {
                ctx.error = glz::error_code::syntax_error;
                return;
            }
            u = *parsed;
        },
        [](const aegon::data::types::UUID& u) -> std::string {
            return u.to_string();
        }
    >;
};
#endif

// Include generator header for full UUID ecosystem
#include "data/types/UUIDGenerator.h"
