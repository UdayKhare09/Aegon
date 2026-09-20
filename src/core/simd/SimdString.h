#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <cstring>
#include <bit>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace aegon::core::simd {

/**
 * @brief Hardware-accelerated string and buffer scanning primitives with tiered fallbacks:
 * Tier 1: AVX-512BW / AVX-512VL (64 bytes/cycle with dedicated k-mask registers)
 * Tier 2: AVX2 + BMI2 (32 bytes/cycle with movemask + tzcnt)
 * Tier 3: SSE4.2 (16 bytes/cycle)
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
#else
        size_t i = 0;
#endif
        // Scalar remainder
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
        if (pos + 1 >= sv.size()) return std::string_view::npos;
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
        if (pos + 3 >= sv.size()) return std::string_view::npos;
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
     */
    static inline bool iequals(std::string_view a, std::string_view b) noexcept {
        const size_t len = a.size();
        if (len != b.size()) return false;
        if (len == 0) return true;

        const char* p1 = a.data();
        const char* p2 = b.data();

#if defined(__AVX512VL__) && defined(__AVX512BW__)
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
        }
#endif

        // Scalar fallback
        for (size_t i = 0; i < len; ++i) {
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
            __mmask64 mask_tail = (1ULL << (len - i)) - 1ULL;
            __m512i chunk = _mm512_maskz_loadu_epi8(mask_tail, ptr + i);
            uint64_t mask = _mm512_cmpeq_epi8_mask(chunk, pct) | _mm512_cmpeq_epi8_mask(chunk, plus);
            if (mask != 0) return true;
        }
        return false;
#else
        for (char c : sv) {
            if (c == '%' || c == '+') return true;
        }
        return false;
#endif
    }
};

} // namespace aegon::core::simd
