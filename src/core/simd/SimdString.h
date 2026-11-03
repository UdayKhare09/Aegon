#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <cstring>
#include <bit>
#include <array>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace aegon::core::simd {

namespace detail {
#if defined(__ARM_NEON) || defined(__aarch64__)
inline uint32_t neon_movemask_u8(uint8x16_t input) noexcept {
    const uint8x16_t bitmask = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
    };
    uint8x16_t masked = vandq_u8(input, bitmask);
#if defined(__aarch64__)
    uint8_t lo = vaddv_u8(vget_low_u8(masked));
    uint8_t hi = vaddv_u8(vget_high_u8(masked));
    return static_cast<uint32_t>(lo) | (static_cast<uint32_t>(hi) << 8);
#else
    uint8x8_t lo = vget_low_u8(masked);
    uint8x8_t hi = vget_high_u8(masked);
    uint16x4_t sum_lo = vpaddl_u8(lo);
    uint16x4_t sum_hi = vpaddl_u8(hi);
    uint32x2_t s_lo = vpaddl_u16(sum_lo);
    uint32x2_t s_hi = vpaddl_u16(sum_hi);
    uint64x1_t f_lo = vpaddl_u32(s_lo);
    uint64x1_t f_hi = vpaddl_u32(s_hi);
    return static_cast<uint32_t>(vget_lane_u64(f_lo, 0)) | (static_cast<uint32_t>(vget_lane_u64(f_hi, 0)) << 8);
#endif
}
#endif
} // namespace detail

/**
 * @brief Hardware-accelerated string and buffer scanning primitives with tiered fallbacks:
 * Tier 1: AVX-512BW / AVX-512VL (64 bytes/cycle with dedicated k-mask registers)
 * Tier 2: AVX2 + BMI2 (32 bytes/cycle with movemask + tzcnt)
 * Tier 3: SSE4.2 (x86_64) / ARM NEON (aarch64) (16 bytes/cycle)
 * Tier 4: Portable SWAR / Scalar fallback
 */
class SimdString {
public:
    /**
     * @brief Find first occurrence of single character `c` in `sv`.
     * @return 0-based index or std::string_view::npos.
     */
    static inline size_t find_char(std::string_view sv, char c, size_t pos = 0) noexcept {
        if (pos >= sv.size()) return std::string_view::npos;
        const char* ptr = sv.data() + pos;
        size_t len = sv.size() - pos;

#if defined(__AVX512BW__) && defined(__AVX512VL__)
        const __m512i target = _mm512_set1_epi8(c);
        size_t i = 0;
        for (; i + 64 <= len; i += 64) {
            __m512i chunk = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(ptr + i));
            uint64_t mask = _mm512_cmpeq_epi8_mask(chunk, target);
            if (mask != 0) {
                return pos + i + std::countr_zero(mask);
            }
        }
        // Masked-load tail: handles remaining 1..63 bytes without scalar loop
        if (i < len) {
            __mmask64 tail = (__mmask64)((1ULL << (len - i)) - 1ULL);
            __m512i chunk = _mm512_maskz_loadu_epi8(tail, ptr + i);
            uint64_t mask = _mm512_cmpeq_epi8_mask(chunk, target) & (uint64_t)tail;
            if (mask != 0) return pos + i + std::countr_zero(mask);
        }
        return std::string_view::npos;
#elif defined(__AVX2__)
        const __m256i target = _mm256_set1_epi8(c);
        size_t i = 0;
        for (; i + 32 <= len; i += 32) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + i));
            __m256i cmp = _mm256_cmpeq_epi8(chunk, target);
            uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(cmp));
            if (mask != 0) {
                return pos + i + std::countr_zero(mask);
            }
        }
#elif defined(__SSE4_2__)
        const __m128i target = _mm_set1_epi8(c);
        size_t i = 0;
        for (; i + 16 <= len; i += 16) {
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + i));
            __m128i cmp = _mm_cmpeq_epi8(chunk, target);
            uint32_t mask = static_cast<uint32_t>(_mm_movemask_epi8(cmp));
            if (mask != 0) {
                return pos + i + std::countr_zero(mask);
            }
        }
#elif defined(__ARM_NEON) || defined(__aarch64__)
        const uint8x16_t target = vdupq_n_u8(static_cast<uint8_t>(c));
        size_t i = 0;
        for (; i + 16 <= len; i += 16) {
            uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr + i));
            uint8x16_t cmp = vceqq_u8(chunk, target);
            uint32_t mask = detail::neon_movemask_u8(cmp);
            if (mask != 0) {
                return pos + i + std::countr_zero(mask);
            }
        }
#else
        size_t i = 0;
#endif
        // Scalar remainder (AVX2 / SSE4.2 paths fall through here; AVX-512 returns early)
        for (; i < len; ++i) {
            if (ptr[i] == c) return pos + i;
        }
        return std::string_view::npos;
    }

    /**
     * @brief Find first occurrence of CRLF ("\r\n") in `sv`.
     * @return 0-based index of '\r' or std::string_view::npos.
     */
    static inline size_t find_crlf(std::string_view sv, size_t pos = 0) noexcept {
        if (pos >= sv.size() || (sv.size() - pos) < 2) return std::string_view::npos;
        const char* ptr = sv.data() + pos;
        size_t len = sv.size() - pos;

#if defined(__AVX512BW__) && defined(__AVX512VL__)
        const __m512i cr = _mm512_set1_epi8('\r');
        const __m512i lf = _mm512_set1_epi8('\n');
        size_t i = 0;
        for (; i + 65 <= len; i += 64) {
            __m512i chunk_cr = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(ptr + i));
            __m512i chunk_lf = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(ptr + i + 1));
            uint64_t mask = _mm512_cmpeq_epi8_mask(chunk_cr, cr) & _mm512_cmpeq_epi8_mask(chunk_lf, lf);
            if (mask != 0) {
                return pos + i + std::countr_zero(mask);
            }
        }
        // Masked-load tail: valid start positions are [i, len-2]
        if (i + 1 < len) {
            size_t n = len - i - 1; // number of valid CRLF start positions (1..63)
            __mmask64 tail = (__mmask64)((1ULL << n) - 1ULL);
            __m512i chunk_cr = _mm512_maskz_loadu_epi8(tail, ptr + i);
            __m512i chunk_lf = _mm512_maskz_loadu_epi8(tail, ptr + i + 1);
            uint64_t mask = (uint64_t)(_mm512_cmpeq_epi8_mask(chunk_cr, cr) &
                                        _mm512_cmpeq_epi8_mask(chunk_lf, lf) & tail);
            if (mask != 0) return pos + i + std::countr_zero(mask);
        }
        return std::string_view::npos;
#elif defined(__AVX2__)
        const __m256i cr = _mm256_set1_epi8('\r');
        const __m256i lf = _mm256_set1_epi8('\n');
        size_t i = 0;
        for (; i + 33 <= len; i += 32) {
            __m256i chunk_cr = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + i));
            __m256i chunk_lf = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + i + 1));
            __m256i cmp_cr = _mm256_cmpeq_epi8(chunk_cr, cr);
            __m256i cmp_lf = _mm256_cmpeq_epi8(chunk_lf, lf);
            uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_and_si256(cmp_cr, cmp_lf)));
            if (mask != 0) {
                return pos + i + std::countr_zero(mask);
            }
        }
#elif defined(__SSE4_2__)
        const __m128i cr = _mm_set1_epi8('\r');
        const __m128i lf = _mm_set1_epi8('\n');
        size_t i = 0;
        for (; i + 17 <= len; i += 16) {
            __m128i chunk_cr = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + i));
            __m128i chunk_lf = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + i + 1));
            __m128i cmp_cr = _mm_cmpeq_epi8(chunk_cr, cr);
            __m128i cmp_lf = _mm_cmpeq_epi8(chunk_lf, lf);
            uint32_t mask = static_cast<uint32_t>(_mm_movemask_epi8(_mm_and_si128(cmp_cr, cmp_lf)));
            if (mask != 0) {
                return pos + i + std::countr_zero(mask);
            }
        }
#elif defined(__ARM_NEON) || defined(__aarch64__)
        const uint8x16_t cr = vdupq_n_u8(static_cast<uint8_t>('\r'));
        const uint8x16_t lf = vdupq_n_u8(static_cast<uint8_t>('\n'));
        size_t i = 0;
        for (; i + 17 <= len; i += 16) {
            uint8x16_t chunk_cr = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr + i));
            uint8x16_t chunk_lf = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr + i + 1));
            uint8x16_t cmp_cr = vceqq_u8(chunk_cr, cr);
            uint8x16_t cmp_lf = vceqq_u8(chunk_lf, lf);
            uint8x16_t cmp = vandq_u8(cmp_cr, cmp_lf);
            uint32_t mask = detail::neon_movemask_u8(cmp);
            if (mask != 0) {
                return pos + i + std::countr_zero(mask);
            }
        }
#else
        size_t i = 0;
#endif
        // Scalar remainder
        for (; i + 1 < len; ++i) {
            if (ptr[i] == '\r' && ptr[i + 1] == '\n') return pos + i;
        }
        return std::string_view::npos;
    }

    /**
     * @brief Find double CRLF ("\r\n\r\n") denoting the end of HTTP header block.
     * @return 0-based index of the first '\r' or std::string_view::npos.
     */
    static inline size_t find_double_crlf(std::string_view sv, size_t pos = 0) noexcept {
        if (pos >= sv.size() || (sv.size() - pos) < 4) return std::string_view::npos;
        const char* ptr = sv.data() + pos;
        size_t len = sv.size() - pos;

#if defined(__AVX512BW__) && defined(__AVX512VL__)
        const __m512i cr = _mm512_set1_epi8('\r');
        const __m512i lf = _mm512_set1_epi8('\n');
        size_t i = 0;
        for (; i + 67 <= len; i += 64) {
            __m512i c0 = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(ptr + i));
            __m512i c1 = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(ptr + i + 1));
            __m512i c2 = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(ptr + i + 2));
            __m512i c3 = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(ptr + i + 3));
            uint64_t mask = _mm512_cmpeq_epi8_mask(c0, cr) &
                            _mm512_cmpeq_epi8_mask(c1, lf) &
                            _mm512_cmpeq_epi8_mask(c2, cr) &
                            _mm512_cmpeq_epi8_mask(c3, lf);
            if (mask != 0) {
                return pos + i + std::countr_zero(mask);
            }
        }
        // Masked-load tail: valid start positions are [i, len-4]
        if (i + 3 < len) {
            size_t n = len - i - 3; // number of valid start positions (1..63)
            __mmask64 tail = (__mmask64)((1ULL << n) - 1ULL);
            __m512i c0 = _mm512_maskz_loadu_epi8(tail, ptr + i);
            __m512i c1 = _mm512_maskz_loadu_epi8(tail, ptr + i + 1);
            __m512i c2 = _mm512_maskz_loadu_epi8(tail, ptr + i + 2);
            __m512i c3 = _mm512_maskz_loadu_epi8(tail, ptr + i + 3);
            uint64_t mask = (uint64_t)(_mm512_cmpeq_epi8_mask(c0, cr) &
                                        _mm512_cmpeq_epi8_mask(c1, lf) &
                                        _mm512_cmpeq_epi8_mask(c2, cr) &
                                        _mm512_cmpeq_epi8_mask(c3, lf) & tail);
            if (mask != 0) return pos + i + std::countr_zero(mask);
        }
        return std::string_view::npos;
#elif defined(__AVX2__)
        const __m256i cr = _mm256_set1_epi8('\r');
        const __m256i lf = _mm256_set1_epi8('\n');
        size_t i = 0;
        for (; i + 35 <= len; i += 32) {
            __m256i c0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + i));
            __m256i c1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + i + 1));
            __m256i c2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + i + 2));
            __m256i c3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + i + 3));
            __m256i m0 = _mm256_and_si256(_mm256_cmpeq_epi8(c0, cr), _mm256_cmpeq_epi8(c1, lf));
            __m256i m1 = _mm256_and_si256(_mm256_cmpeq_epi8(c2, cr), _mm256_cmpeq_epi8(c3, lf));
            uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_and_si256(m0, m1)));
            if (mask != 0) {
                return pos + i + std::countr_zero(mask);
            }
        }
#elif defined(__SSE4_2__)
        const __m128i cr = _mm_set1_epi8('\r');
        const __m128i lf = _mm_set1_epi8('\n');
        size_t i = 0;
        for (; i + 19 <= len; i += 16) {
            __m128i c0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + i));
            __m128i c1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + i + 1));
            __m128i c2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + i + 2));
            __m128i c3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + i + 3));
            __m128i m0 = _mm_and_si128(_mm_cmpeq_epi8(c0, cr), _mm_cmpeq_epi8(c1, lf));
            __m128i m1 = _mm_and_si128(_mm_cmpeq_epi8(c2, cr), _mm_cmpeq_epi8(c3, lf));
            uint32_t mask = static_cast<uint32_t>(_mm_movemask_epi8(_mm_and_si128(m0, m1)));
            if (mask != 0) {
                return pos + i + std::countr_zero(mask);
            }
        }
#elif defined(__ARM_NEON) || defined(__aarch64__)
        const uint8x16_t cr = vdupq_n_u8(static_cast<uint8_t>('\r'));
        const uint8x16_t lf = vdupq_n_u8(static_cast<uint8_t>('\n'));
        size_t i = 0;
        for (; i + 19 <= len; i += 16) {
            uint8x16_t c0 = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr + i));
            uint8x16_t c1 = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr + i + 1));
            uint8x16_t c2 = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr + i + 2));
            uint8x16_t c3 = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr + i + 3));
            uint8x16_t m0 = vandq_u8(vceqq_u8(c0, cr), vceqq_u8(c1, lf));
            uint8x16_t m1 = vandq_u8(vceqq_u8(c2, cr), vceqq_u8(c3, lf));
            uint8x16_t cmp = vandq_u8(m0, m1);
            uint32_t mask = detail::neon_movemask_u8(cmp);
            if (mask != 0) {
                return pos + i + std::countr_zero(mask);
            }
        }
#else
        size_t i = 0;
#endif
        // Scalar remainder
        for (; i + 3 < len; ++i) {
            if (ptr[i] == '\r' && ptr[i + 1] == '\n' && ptr[i + 2] == '\r' && ptr[i + 3] == '\n') {
                return pos + i;
            }
        }
        return std::string_view::npos;
    }

    /**
     * @brief Case-insensitive ASCII equality comparison accelerated with SIMD.
     *
     * Fixes:
     *  - AVX-512: added full 64-byte loop + masked-load tail for len > 32
     *    (previously fell straight to scalar for any len > 32)
     *  - SSE4.2:  fixed movemask comparison to avoid sign-extension artifacts
     */
    static inline bool iequals(std::string_view a, std::string_view b) noexcept {
        const size_t len = a.size();
        if (len != b.size()) return false;
        if (len == 0) return true;

        const char* p1 = a.data();
        const char* p2 = b.data();

#if defined(__AVX512VL__) && defined(__AVX512BW__)
        // Shared constants for all sub-paths
        const __m512i upper_a_v = _mm512_set1_epi8('A' - 1);
        const __m512i upper_z_v = _mm512_set1_epi8('Z' + 1);
        const __m512i to_lower_v = _mm512_set1_epi8(32);

        size_t i = 0; // declared here so the (dead) scalar fallback after #endif compiles

        if (len <= 16) {
            __mmask16 mask = (1U << len) - 1U;
            __m128i v1 = _mm_maskz_loadu_epi8(mask, p1);
            __m128i v2 = _mm_maskz_loadu_epi8(mask, p2);
            __m128i upper_a = _mm_set1_epi8('A' - 1);
            __m128i upper_z = _mm_set1_epi8('Z' + 1);
            __mmask16 is_upper1 = _mm_cmpgt_epi8_mask(v1, upper_a) & _mm_cmplt_epi8_mask(v1, upper_z);
            __mmask16 is_upper2 = _mm_cmpgt_epi8_mask(v2, upper_a) & _mm_cmplt_epi8_mask(v2, upper_z);
            v1 = _mm_mask_add_epi8(v1, is_upper1, v1, _mm_set1_epi8(32));
            v2 = _mm_mask_add_epi8(v2, is_upper2, v2, _mm_set1_epi8(32));
            __mmask16 eq_mask = _mm_cmpeq_epi8_mask(v1, v2);
            return (eq_mask & mask) == mask;
        } else if (len <= 32) {
            __mmask32 mask = static_cast<__mmask32>((1ULL << len) - 1ULL);
            __m256i v1 = _mm256_maskz_loadu_epi8(mask, p1);
            __m256i v2 = _mm256_maskz_loadu_epi8(mask, p2);
            __m256i upper_a = _mm256_set1_epi8('A' - 1);
            __m256i upper_z = _mm256_set1_epi8('Z' + 1);
            __mmask32 is_upper1 = _mm256_cmpgt_epi8_mask(v1, upper_a) & _mm256_cmplt_epi8_mask(v1, upper_z);
            __mmask32 is_upper2 = _mm256_cmpgt_epi8_mask(v2, upper_a) & _mm256_cmplt_epi8_mask(v2, upper_z);
            v1 = _mm256_mask_add_epi8(v1, is_upper1, v1, _mm256_set1_epi8(32));
            v2 = _mm256_mask_add_epi8(v2, is_upper2, v2, _mm256_set1_epi8(32));
            __mmask32 eq_mask = _mm256_cmpeq_epi8_mask(v1, v2);
            return (eq_mask & mask) == mask;
        } else {
            // len > 32: full 64-byte AVX-512 loop with masked-load tail
            for (; i + 64 <= len; i += 64) {
                __m512i v1 = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(p1 + i));
                __m512i v2 = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(p2 + i));
                __mmask64 is_u1 = _mm512_cmpgt_epi8_mask(v1, upper_a_v) & _mm512_cmplt_epi8_mask(v1, upper_z_v);
                __mmask64 is_u2 = _mm512_cmpgt_epi8_mask(v2, upper_a_v) & _mm512_cmplt_epi8_mask(v2, upper_z_v);
                v1 = _mm512_mask_add_epi8(v1, is_u1, v1, to_lower_v);
                v2 = _mm512_mask_add_epi8(v2, is_u2, v2, to_lower_v);
                if (_mm512_cmpeq_epi8_mask(v1, v2) != ~(__mmask64)0) return false;
            }
            // Masked-load tail: remaining 1..63 bytes
            if (i < len) {
                size_t rem = len - i;
                __mmask64 tail = (__mmask64)((1ULL << rem) - 1ULL);
                __m512i v1 = _mm512_maskz_loadu_epi8(tail, p1 + i);
                __m512i v2 = _mm512_maskz_loadu_epi8(tail, p2 + i);
                __mmask64 is_u1 = _mm512_cmpgt_epi8_mask(v1, upper_a_v) & _mm512_cmplt_epi8_mask(v1, upper_z_v);
                __mmask64 is_u2 = _mm512_cmpgt_epi8_mask(v2, upper_a_v) & _mm512_cmplt_epi8_mask(v2, upper_z_v);
                v1 = _mm512_mask_add_epi8(v1, is_u1, v1, to_lower_v);
                v2 = _mm512_mask_add_epi8(v2, is_u2, v2, to_lower_v);
                if ((_mm512_cmpeq_epi8_mask(v1, v2) & tail) != tail) return false;
            }
            return true; // all 64-byte chunks and tail matched
        }
        // All branches above return — scalar loop below is dead code for AVX-512 path
#elif defined(__AVX2__)
        size_t i = 0;
        if (len >= 32) {
            const __m256i upper_a = _mm256_set1_epi8('A' - 1);
            const __m256i upper_z = _mm256_set1_epi8('Z' + 1);
            const __m256i to_lower = _mm256_set1_epi8(32);

            for (; i + 32 <= len; i += 32) {
                __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p1 + i));
                __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p2 + i));

                __m256i is_u1 = _mm256_and_si256(_mm256_cmpgt_epi8(v1, upper_a), _mm256_cmpgt_epi8(upper_z, v1));
                __m256i is_u2 = _mm256_and_si256(_mm256_cmpgt_epi8(v2, upper_a), _mm256_cmpgt_epi8(upper_z, v2));

                v1 = _mm256_or_si256(v1, _mm256_and_si256(is_u1, to_lower));
                v2 = _mm256_or_si256(v2, _mm256_and_si256(is_u2, to_lower));

                __m256i cmp = _mm256_cmpeq_epi8(v1, v2);
                if (static_cast<uint32_t>(_mm256_movemask_epi8(cmp)) != 0xFFFFFFFFU) {
                    return false;
                }
            }
        }
#elif defined(__SSE4_2__)
        size_t i = 0;
        if (len >= 16) {
            const __m128i upper_a = _mm_set1_epi8('A' - 1);
            const __m128i upper_z = _mm_set1_epi8('Z' + 1);
            const __m128i to_lower = _mm_set1_epi8(32);

            for (; i + 16 <= len; i += 16) {
                __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p1 + i));
                __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p2 + i));

                __m128i is_u1 = _mm_and_si128(_mm_cmpgt_epi8(v1, upper_a), _mm_cmpgt_epi8(upper_z, v1));
                __m128i is_u2 = _mm_and_si128(_mm_cmpgt_epi8(v2, upper_a), _mm_cmpgt_epi8(upper_z, v2));

                v1 = _mm_or_si128(v1, _mm_and_si128(is_u1, to_lower));
                v2 = _mm_or_si128(v2, _mm_and_si128(is_u2, to_lower));

                __m128i cmp = _mm_cmpeq_epi8(v1, v2);
                // FIX: mask to 16 bits before comparison to avoid sign-extension artifacts
                if ((_mm_movemask_epi8(cmp) & 0xFFFF) != 0xFFFF) {
                    return false;
                }
            }
        }
#elif defined(__ARM_NEON) || defined(__aarch64__)
        size_t i = 0;
        if (len >= 16) {
            const uint8x16_t c_A = vdupq_n_u8('A');
            const uint8x16_t c_Z = vdupq_n_u8('Z');
            const uint8x16_t to_lower = vdupq_n_u8(32);

            for (; i + 16 <= len; i += 16) {
                uint8x16_t v1 = vld1q_u8(reinterpret_cast<const uint8_t*>(p1 + i));
                uint8x16_t v2 = vld1q_u8(reinterpret_cast<const uint8_t*>(p2 + i));

                uint8x16_t is_u1 = vandq_u8(vcgeq_u8(v1, c_A), vcleq_u8(v1, c_Z));
                uint8x16_t is_u2 = vandq_u8(vcgeq_u8(v2, c_A), vcleq_u8(v2, c_Z));

                v1 = vorrq_u8(v1, vandq_u8(is_u1, to_lower));
                v2 = vorrq_u8(v2, vandq_u8(is_u2, to_lower));

                uint8x16_t cmp = vceqq_u8(v1, v2);
#if defined(__aarch64__)
                if (vminvq_u8(cmp) != 0xFF) return false;
#else
                if ((detail::neon_movemask_u8(cmp) & 0xFFFF) != 0xFFFF) return false;
#endif
            }
        }
#else
        size_t i = 0;
#endif

        // Scalar remainder (reached by AVX2 and SSE4.2 paths; dead code for AVX-512)
        for (; i < len; ++i) {
            char ca = p1[i];
            char cb = p2[i];
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) return false;
        }
        return true;
    }

    /**
     * @brief Quickly checks if a string contains '%' or '+' requiring URL decoding.
     */
    static inline bool has_url_encoding_chars(std::string_view sv) noexcept {
        const size_t len = sv.size();
        const char* ptr = sv.data();

#if defined(__AVX512VL__) && defined(__AVX512BW__)
        const __m512i pct = _mm512_set1_epi8('%');
        const __m512i plus = _mm512_set1_epi8('+');
        size_t i = 0;
        for (; i + 64 <= len; i += 64) {
            __m512i chunk = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(ptr + i));
            uint64_t mask = _mm512_cmpeq_epi8_mask(chunk, pct) | _mm512_cmpeq_epi8_mask(chunk, plus);
            if (mask != 0) return true;
        }
        if (i < len) {
            __mmask64 mask_tail = (__mmask64)((1ULL << (len - i)) - 1ULL);
            __m512i chunk = _mm512_maskz_loadu_epi8(mask_tail, ptr + i);
            uint64_t mask = _mm512_cmpeq_epi8_mask(chunk, pct) | _mm512_cmpeq_epi8_mask(chunk, plus);
            if (mask != 0) return true;
        }
        return false;
#elif defined(__AVX2__)
        const __m256i pct = _mm256_set1_epi8('%');
        const __m256i plus = _mm256_set1_epi8('+');
        size_t i = 0;
        for (; i + 32 <= len; i += 32) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + i));
            __m256i cmp = _mm256_or_si256(_mm256_cmpeq_epi8(chunk, pct), _mm256_cmpeq_epi8(chunk, plus));
            if (_mm256_movemask_epi8(cmp) != 0) return true;
        }
        for (; i < len; ++i) {
            if (ptr[i] == '%' || ptr[i] == '+') return true;
        }
        return false;
#elif defined(__SSE4_2__)
        const __m128i pct = _mm_set1_epi8('%');
        const __m128i plus = _mm_set1_epi8('+');
        size_t i = 0;
        for (; i + 16 <= len; i += 16) {
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + i));
            __m128i cmp = _mm_or_si128(_mm_cmpeq_epi8(chunk, pct), _mm_cmpeq_epi8(chunk, plus));
            if (_mm_movemask_epi8(cmp) != 0) return true;
        }
        for (; i < len; ++i) {
            if (ptr[i] == '%' || ptr[i] == '+') return true;
        }
        return false;
#elif defined(__ARM_NEON) || defined(__aarch64__)
        const uint8x16_t pct = vdupq_n_u8(static_cast<uint8_t>('%'));
        const uint8x16_t plus = vdupq_n_u8(static_cast<uint8_t>('+'));
        size_t i = 0;
        for (; i + 16 <= len; i += 16) {
            uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr + i));
            uint8x16_t cmp = vorrq_u8(vceqq_u8(chunk, pct), vceqq_u8(chunk, plus));
#if defined(__aarch64__)
            if (vmaxvq_u8(cmp) != 0) return true;
#else
            if (detail::neon_movemask_u8(cmp) != 0) return true;
#endif
        }
        for (; i < len; ++i) {
            if (ptr[i] == '%' || ptr[i] == '+') return true;
        }
        return false;
#else
        (void)len;
        (void)ptr;
        for (char c : sv) {
            if (c == '%' || c == '+') return true;
        }
        return false;
#endif
    }

    /**
     * @brief Lookup table for RFC 9110 tchar validation
     */
    static constexpr auto TCHAR_TABLE = []() consteval {
        std::array<bool, 256> table{};
        for (int i = 0; i < 256; ++i) {
            char c = static_cast<char>(i);
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
                      c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' ||
                      c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
            table[static_cast<size_t>(i)] = ok;
        }
        return table;
    }();

    /**
     * @brief Validate that all characters in sv are valid RFC 9110 tchar tokens.
     */
    static inline bool validate_tchars(std::string_view sv) noexcept {
        for (char c : sv) {
            if (!TCHAR_TABLE[static_cast<uint8_t>(c)]) return false;
        }
        return true;
    }

    /**
     * @brief Validate request URI characters: must be printable ASCII without CTLs, space, or DEL (0x21..0x7E).
     */
    static inline bool validate_uri_chars(std::string_view sv) noexcept {
        const size_t len = sv.size();
        const char* ptr = sv.data();
        size_t i = 0;

#if defined(__AVX512VL__) && defined(__AVX512BW__)
        const __m512i low = _mm512_set1_epi8(0x20);
        const __m512i high = _mm512_set1_epi8(0x7F);
        for (; i + 64 <= len; i += 64) {
            __m512i chunk = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(ptr + i));
            uint64_t mask = _mm512_cmpgt_epi8_mask(chunk, low) & _mm512_cmplt_epi8_mask(chunk, high);
            if (mask != ~0ULL) return false;
        }
        if (i < len) {
            __mmask64 tail = (__mmask64)((1ULL << (len - i)) - 1ULL);
            __m512i chunk = _mm512_maskz_loadu_epi8(tail, ptr + i);
            uint64_t mask = _mm512_cmpgt_epi8_mask(chunk, low) & _mm512_cmplt_epi8_mask(chunk, high) & tail;
            if (mask != tail) return false;
            return true;
        }
#elif defined(__AVX2__)
        const __m256i low = _mm256_set1_epi8(0x20);
        const __m256i high = _mm256_set1_epi8(0x7F);
        for (; i + 32 <= len; i += 32) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + i));
            __m256i is_gt = _mm256_cmpgt_epi8(chunk, low);
            __m256i is_lt = _mm256_cmpgt_epi8(high, chunk);
            __m256i valid = _mm256_and_si256(is_gt, is_lt);
            if (static_cast<uint32_t>(_mm256_movemask_epi8(valid)) != 0xFFFFFFFFU) return false;
        }
#elif defined(__SSE4_2__)
        const __m128i low = _mm_set1_epi8(0x20);
        const __m128i high = _mm_set1_epi8(0x7F);
        for (; i + 16 <= len; i += 16) {
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + i));
            __m128i is_gt = _mm_cmpgt_epi8(chunk, low);
            __m128i is_lt = _mm_cmpgt_epi8(high, chunk);
            __m128i valid = _mm_and_si128(is_gt, is_lt);
            if ((_mm_movemask_epi8(valid) & 0xFFFF) != 0xFFFF) return false;
        }
#elif defined(__ARM_NEON) || defined(__aarch64__)
        const uint8x16_t low = vdupq_n_u8(0x20);
        const uint8x16_t high = vdupq_n_u8(0x7F);
        for (; i + 16 <= len; i += 16) {
            uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr + i));
            uint8x16_t is_gt = vcgtq_u8(chunk, low);
            uint8x16_t is_lt = vcltq_u8(chunk, high);
            uint8x16_t valid = vandq_u8(is_gt, is_lt);
#if defined(__aarch64__)
            if (vminvq_u8(valid) != 0xFF) return false;
#else
            if ((detail::neon_movemask_u8(valid) & 0xFFFF) != 0xFFFF) return false;
#endif
        }
#endif

        for (; i < len; ++i) {
            unsigned char uc = static_cast<unsigned char>(ptr[i]);
            if (uc <= 0x20 || uc >= 0x7F) return false;
        }
        return true;
    }

    /**
     * @brief Validate HTTP header value per RFC 9112 §2.2 / RFC 9110 §5.5:
     * Rejects bare CR ('\r'), NUL, and CTLs (< 0x20 except '\t'), and DEL (0x7F).
     */
    static inline bool validate_header_value(std::string_view sv) noexcept {
        const size_t len = sv.size();
        const char* ptr = sv.data();
        size_t i = 0;

#if defined(__AVX2__)
        const __m256i cr = _mm256_set1_epi8('\r');
        const __m256i del = _mm256_set1_epi8(0x7F);
        const __m256i tab = _mm256_set1_epi8('\t');
        const __m256i ctl_limit = _mm256_set1_epi8(0x20);

        for (; i + 32 <= len; i += 32) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + i));
            __m256i is_cr = _mm256_cmpeq_epi8(chunk, cr);
            __m256i is_del = _mm256_cmpeq_epi8(chunk, del);
            __m256i is_tab = _mm256_cmpeq_epi8(chunk, tab);
            __m256i is_ctl = _mm256_cmpgt_epi8(_mm256_subs_epu8(ctl_limit, chunk), _mm256_setzero_si256());
            __m256i bad_ctl = _mm256_andnot_si256(is_tab, is_ctl);
            __m256i bad = _mm256_or_si256(_mm256_or_si256(is_cr, is_del), bad_ctl);
            if (_mm256_movemask_epi8(bad) != 0) return false;
        }
#elif defined(__SSE4_2__)
        const __m128i cr = _mm_set1_epi8('\r');
        const __m128i del = _mm_set1_epi8(0x7F);
        const __m128i tab = _mm_set1_epi8('\t');
        const __m128i ctl_limit = _mm_set1_epi8(0x20);

        for (; i + 16 <= len; i += 16) {
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + i));
            __m128i is_cr = _mm_cmpeq_epi8(chunk, cr);
            __m128i is_del = _mm_cmpeq_epi8(chunk, del);
            __m128i is_tab = _mm_cmpeq_epi8(chunk, tab);
            __m128i is_ctl = _mm_cmpgt_epi8(_mm_subs_epu8(ctl_limit, chunk), _mm_setzero_si128());
            __m128i bad_ctl = _mm_andnot_si128(is_tab, is_ctl);
            __m128i bad = _mm_or_si128(_mm_or_si128(is_cr, is_del), bad_ctl);
            if ((_mm_movemask_epi8(bad) & 0xFFFF) != 0) return false;
        }
#elif defined(__ARM_NEON) || defined(__aarch64__)
        const uint8x16_t cr = vdupq_n_u8(static_cast<uint8_t>('\r'));
        const uint8x16_t del = vdupq_n_u8(0x7F);
        const uint8x16_t tab = vdupq_n_u8(static_cast<uint8_t>('\t'));
        const uint8x16_t ctl_limit = vdupq_n_u8(0x20);

        for (; i + 16 <= len; i += 16) {
            uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr + i));
            uint8x16_t is_cr = vceqq_u8(chunk, cr);
            uint8x16_t is_del = vceqq_u8(chunk, del);
            uint8x16_t is_tab = vceqq_u8(chunk, tab);
            uint8x16_t is_ctl = vcltq_u8(chunk, ctl_limit);
            uint8x16_t bad_ctl = vbicq_u8(is_ctl, is_tab);
            uint8x16_t bad = vorrq_u8(vorrq_u8(is_cr, is_del), bad_ctl);
#if defined(__aarch64__)
            if (vmaxvq_u8(bad) != 0) return false;
#else
            if ((detail::neon_movemask_u8(bad) & 0xFFFF) != 0) return false;
#endif
        }
#endif

        for (; i < len; ++i) {
            unsigned char uc = static_cast<unsigned char>(ptr[i]);
            if (uc == '\r' || (uc < 0x20 && uc != '\t') || uc == 0x7F) return false;
        }
        return true;
    }

    /**
     * @brief Validate HTTP/2 and HTTP/3 header values: must not contain '\0', '\r', or '\n'.
     */
    static inline bool validate_h2_h3_header_value(std::string_view sv) noexcept {
        const size_t len = sv.size();
        const char* ptr = sv.data();
        size_t i = 0;

#if defined(__AVX2__)
        const __m256i zero = _mm256_setzero_si256();
        const __m256i cr = _mm256_set1_epi8('\r');
        const __m256i lf = _mm256_set1_epi8('\n');

        for (; i + 32 <= len; i += 32) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + i));
            __m256i is_z = _mm256_cmpeq_epi8(chunk, zero);
            __m256i is_cr = _mm256_cmpeq_epi8(chunk, cr);
            __m256i is_lf = _mm256_cmpeq_epi8(chunk, lf);
            __m256i bad = _mm256_or_si256(_mm256_or_si256(is_z, is_cr), is_lf);
            if (_mm256_movemask_epi8(bad) != 0) return false;
        }
#elif defined(__SSE4_2__)
        const __m128i zero = _mm_setzero_si128();
        const __m128i cr = _mm_set1_epi8('\r');
        const __m128i lf = _mm_set1_epi8('\n');

        for (; i + 16 <= len; i += 16) {
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + i));
            __m128i is_z = _mm_cmpeq_epi8(chunk, zero);
            __m128i is_cr = _mm_cmpeq_epi8(chunk, cr);
            __m128i is_lf = _mm_cmpeq_epi8(chunk, lf);
            __m128i bad = _mm_or_si128(_mm_or_si128(is_z, is_cr), is_lf);
            if ((_mm_movemask_epi8(bad) & 0xFFFF) != 0) return false;
        }
#elif defined(__ARM_NEON) || defined(__aarch64__)
        const uint8x16_t zero = vdupq_n_u8(0);
        const uint8x16_t cr = vdupq_n_u8(static_cast<uint8_t>('\r'));
        const uint8x16_t lf = vdupq_n_u8(static_cast<uint8_t>('\n'));

        for (; i + 16 <= len; i += 16) {
            uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr + i));
            uint8x16_t is_z = vceqq_u8(chunk, zero);
            uint8x16_t is_cr = vceqq_u8(chunk, cr);
            uint8x16_t is_lf = vceqq_u8(chunk, lf);
            uint8x16_t bad = vorrq_u8(vorrq_u8(is_z, is_cr), is_lf);
#if defined(__aarch64__)
            if (vmaxvq_u8(bad) != 0) return false;
#else
            if ((detail::neon_movemask_u8(bad) & 0xFFFF) != 0) return false;
#endif
        }
#endif

        for (; i < len; ++i) {
            unsigned char uc = static_cast<unsigned char>(ptr[i]);
            if (uc == '\0' || uc == '\r' || uc == '\n') return false;
        }
        return true;
    }
};

} // namespace aegon::core::simd
