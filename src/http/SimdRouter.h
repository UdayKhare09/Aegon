#pragma once

#include <string_view>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <bit>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace aegon::http::simd {

/**
 * @brief Computes the length of the common prefix between two string_views using SIMD.
 * 
 * Uses AVX-512 (VL, BW) / AVX2 / SSE4.2 vector instructions with 64-bit XOR and scalar fallbacks.
 */
inline size_t simd_common_prefix(std::string_view a, std::string_view b) noexcept {
    size_t len = 0;
    const size_t max_len = std::min(a.size(), b.size());
    const char* p1 = a.data();
    const char* p2 = b.data();

#if defined(__AVX512VL__) && defined(__AVX512BW__)
    while (len + 32 <= max_len) {
        __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p1 + len));
        __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p2 + len));
        __mmask32 mask = _mm256_cmpeq_epi8_mask(v1, v2);
        if (mask != 0xFFFFFFFFU) {
            return len + std::countr_zero(~mask);
        }
        len += 32;
    }
#elif defined(__AVX2__)
    while (len + 32 <= max_len) {
        __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p1 + len));
        __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p2 + len));
        __m256i cmp = _mm256_cmpeq_epi8(v1, v2);
        uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(cmp));
        if (mask != 0xFFFFFFFFU) {
            return len + std::countr_zero(~mask);
        }
        len += 32;
    }
#elif defined(__SSE4_2__) || defined(__SSE2__)
    while (len + 16 <= max_len) {
        __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p1 + len));
        __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p2 + len));
        __m128i cmp = _mm_cmpeq_epi8(v1, v2);
        uint32_t mask = static_cast<uint32_t>(_mm_movemask_epi8(cmp)) & 0xFFFFU;
        if (mask != 0xFFFFU) {
            return len + std::countr_zero((~mask) & 0xFFFFU);
        }
        len += 16;
    }
#endif

    // Fast 64-bit comparison for remaining chunks >= 8 bytes
    while (len + 8 <= max_len) {
        uint64_t u1, u2;
        std::memcpy(&u1, p1 + len, sizeof(uint64_t));
        std::memcpy(&u2, p2 + len, sizeof(uint64_t));
        uint64_t diff = u1 ^ u2;
        if (diff != 0) {
            return len + (std::countr_zero(diff) >> 3);
        }
        len += 8;
    }

    // Scalar tail
    while (len < max_len && p1[len] == p2[len]) {
        ++len;
    }
    return len;
}

/**
 * @brief Finds the first occurrence of a character using 32-byte SIMD vector scans.
 */
inline size_t simd_find_char(std::string_view s, char ch) noexcept {
    const char* p = s.data();
    const size_t size = s.size();
    size_t offset = 0;

#if defined(__AVX512VL__) && defined(__AVX512BW__)
    const __m256i target = _mm256_set1_epi8(ch);
    while (offset + 32 <= size) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + offset));
        __mmask32 mask = _mm256_cmpeq_epi8_mask(chunk, target);
        if (mask != 0) {
            return offset + std::countr_zero(mask);
        }
        offset += 32;
    }
#elif defined(__AVX2__)
    const __m256i target = _mm256_set1_epi8(ch);
    while (offset + 32 <= size) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + offset));
        __m256i cmp = _mm256_cmpeq_epi8(chunk, target);
        uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(cmp));
        if (mask != 0) {
            return offset + std::countr_zero(mask);
        }
        offset += 32;
    }
#elif defined(__SSE4_2__) || defined(__SSE2__)
    const __m128i target = _mm_set1_epi8(ch);
    while (offset + 16 <= size) {
        __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + offset));
        __m128i cmp = _mm_cmpeq_epi8(chunk, target);
        uint32_t mask = static_cast<uint32_t>(_mm_movemask_epi8(cmp)) & 0xFFFFU;
        if (mask != 0) {
            return offset + std::countr_zero(mask);
        }
        offset += 16;
    }
#endif

    while (offset < size) {
        if (p[offset] == ch) return offset;
        ++offset;
    }
    return std::string_view::npos;
}

/**
 * @brief Checks if string starts with a prefix using fast memcmp / SIMD prefix length.
 */
inline bool simd_starts_with(std::string_view str, std::string_view prefix) noexcept {
    if (prefix.size() > str.size()) return false;
    if (prefix.empty()) return true;

    if (prefix.size() <= 8) {
        return std::memcmp(str.data(), prefix.data(), prefix.size()) == 0;
    }

    return simd_common_prefix(str, prefix) == prefix.size();
}

} // namespace aegon::http::simd
