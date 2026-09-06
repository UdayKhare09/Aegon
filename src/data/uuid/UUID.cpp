#include "UUID.h"
#include <cstring>
#include <array>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace aegon::data {

namespace {

// Lookup table for ASCII hex characters
constexpr char HEX_DIGITS_LOWER[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

// 256-entry lookup table for ultra-fast, branchless portable ASCII hex decoding
// Returns 0x00..0x0F for valid hex characters, 0xFF for invalid
constexpr auto make_hex_decode_table() {
    std::array<uint8_t, 256> table{};
    table.fill(0xFF);
    for (uint8_t i = 0; i <= 9; ++i) {
        table[static_cast<size_t>('0' + i)] = i;
    }
    for (uint8_t i = 0; i < 6; ++i) {
        table[static_cast<size_t>('a' + i)] = static_cast<uint8_t>(10 + i);
        table[static_cast<size_t>('A' + i)] = static_cast<uint8_t>(10 + i);
    }
    return table;
}

constexpr auto HEX_DECODE_TABLE = make_hex_decode_table();

#if defined(__AVX512VBMI__) && defined(__AVX512F__)
// AVX-512 VBMI byte permutation table to insert hyphens into 36-char UUID string
// Indices 0..31 map to the 32 hex chars; index 32 maps to '-'
alignas(64) const uint8_t VBMI_FORMAT_PERMUTE[64] = {
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
alignas(64) const uint8_t VBMI_PARSE_PERMUTE[64] = {
    0, 1, 2, 3, 4, 5, 6, 7,      // 0..7
    9, 10, 11, 12,               // 8..11 (skip index 8 '-')
    14, 15, 16, 17,              // 12..15 (skip index 13 '-')
    19, 20, 21, 22,              // 16..19 (skip index 18 '-')
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, // 20..31 (skip index 23 '-')
    // Padding
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
#endif

} // anonymous namespace

void UUID::to_chars(char* out) const noexcept {
#if defined(__AVX512VBMI__) && defined(__AVX512F__) && defined(__AVX512BW__)
    // -------------------------------------------------------------
    // Tier 1: AVX-512 VBMI (AMD Zen 4/5, Intel Xeon Scalable)
    // -------------------------------------------------------------
    __m128i raw = _mm_load_si128(reinterpret_cast<const __m128i*>(data.data()));

    __m128i mask_0f = _mm_set1_epi8(0x0F);
    __m128i hi_nibbles = _mm_and_si128(_mm_srli_epi16(raw, 4), mask_0f);
    __m128i lo_nibbles = _mm_and_si128(raw, mask_0f);

    __m128i nibbles_lo = _mm_unpacklo_epi8(hi_nibbles, lo_nibbles);
    __m128i nibbles_hi = _mm_unpackhi_epi8(hi_nibbles, lo_nibbles);

    __m128i hex_lut = _mm_load_si128(reinterpret_cast<const __m128i*>(HEX_DIGITS_LOWER));
    __m128i ascii_lo = _mm_shuffle_epi8(hex_lut, nibbles_lo);
    __m128i ascii_hi = _mm_shuffle_epi8(hex_lut, nibbles_hi);

    __m256i ascii_256 = _mm256_set_m128i(ascii_hi, ascii_lo);
    __m512i ascii_512 = _mm512_castsi256_si512(ascii_256);
    ascii_512 = _mm512_mask_set1_epi8(ascii_512, 1ULL << 32, '-');

    __m512i permute_idx = _mm512_load_si512(reinterpret_cast<const __m512i*>(VBMI_FORMAT_PERMUTE));
    __m512i formatted = _mm512_permutexvar_epi8(permute_idx, ascii_512);

    const uint64_t mask_36 = (1ULL << 36) - 1;
    _mm512_mask_storeu_epi8(out, mask_36, formatted);

#elif defined(__SSSE3__)
    // -------------------------------------------------------------
    // Tier 2: SSSE3 / AVX2 (Intel Haswell+, Alder/Raptor Lake, Zen 1/2/3)
    // -------------------------------------------------------------
    __m128i raw = _mm_load_si128(reinterpret_cast<const __m128i*>(data.data()));
    __m128i mask_0f = _mm_set1_epi8(0x0F);
    __m128i hi = _mm_and_si128(_mm_srli_epi16(raw, 4), mask_0f);
    __m128i lo = _mm_and_si128(raw, mask_0f);

    __m128i nibbles_0 = _mm_unpacklo_epi8(hi, lo);
    __m128i nibbles_1 = _mm_unpackhi_epi8(hi, lo);

    __m128i hex_lut = _mm_load_si128(reinterpret_cast<const __m128i*>(HEX_DIGITS_LOWER));
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
    // -------------------------------------------------------------
    // Tier 3: Portable Scalar Fallback (Any CPU / ARM / WebAssembly)
    // -------------------------------------------------------------
    size_t out_idx = 0;
    for (size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out[out_idx++] = '-';
        }
        uint8_t b = data[i];
        out[out_idx++] = HEX_DIGITS_LOWER[b >> 4];
        out[out_idx++] = HEX_DIGITS_LOWER[b & 0x0F];
    }
#endif
}

std::string UUID::to_string() const {
    std::string str(36, '\0');
    to_chars(str.data());
    return str;
}

std::optional<UUID> UUID::from_string(std::string_view str) noexcept {
    if (str.size() != 36) [[unlikely]] {
        return std::nullopt;
    }

    const char* s = str.data();

    // Validate hyphens at canonical positions
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') [[unlikely]] {
        return std::nullopt;
    }

#if defined(__AVX512VBMI__) && defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
    // -------------------------------------------------------------
    // Tier 1: AVX-512 VBMI (Full SIMD verification + decode)
    // -------------------------------------------------------------
    const uint64_t mask_36 = (1ULL << 36) - 1;
    __m512i chars_512 = _mm512_maskz_loadu_epi8(mask_36, s);

    __m512i parse_permute = _mm512_load_si512(reinterpret_cast<const __m512i*>(VBMI_PARSE_PERMUTE));
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
        return std::nullopt;
    }

    __m256i digit_val = _mm256_sub_epi8(hex_lower, c_0);
    __m256i alpha_val = _mm256_add_epi8(_mm256_sub_epi8(hex_lower, c_a), _mm256_set1_epi8(10));
    __m256i nibbles_256 = _mm256_blendv_epi8(alpha_val, digit_val, is_digit);

    __m256i mult = _mm256_set1_epi16(0x0110);
    __m256i packed_16bit = _mm256_maddubs_epi16(nibbles_256, mult);

    __m128i half0 = _mm256_castsi256_si128(packed_16bit);
    __m128i half1 = _mm256_extracti128_si256(packed_16bit, 1);
    __m128i bytes_16 = _mm_packus_epi16(half0, half1);

    UUID result;
    _mm_store_si128(reinterpret_cast<__m128i*>(result.data.data()), bytes_16);
    return result;

#else
    // -------------------------------------------------------------
    // Tier 2 & 3: Ultra-Fast Branchless Table-Driven Portable Fallback
    // -------------------------------------------------------------
    UUID result;
    size_t s_idx = 0;
    uint32_t error_acc = 0;

    for (size_t b_idx = 0; b_idx < 16; ++b_idx) {
        if (s_idx == 8 || s_idx == 13 || s_idx == 18 || s_idx == 23) {
            s_idx++;
        }
        uint8_t hi = HEX_DECODE_TABLE[static_cast<uint8_t>(s[s_idx++])];
        uint8_t lo = HEX_DECODE_TABLE[static_cast<uint8_t>(s[s_idx++])];

        // If either character was invalid hex, 0xFF will set top bits
        error_acc |= (hi | lo);
        result.data[b_idx] = static_cast<uint8_t>((hi << 4) | lo);
    }

    if (error_acc & 0xF0) [[unlikely]] {
        return std::nullopt;
    }

    return result;
#endif
}

} // namespace aegon::data
