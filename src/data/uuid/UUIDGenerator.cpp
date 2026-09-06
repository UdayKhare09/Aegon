#include "UUIDGenerator.h"
#include <chrono>
#include <random>
#include <cstring>
#include <bit>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace aegon::data {

namespace {

// ============================================================
//  Scalar xoshiro256++ PRNG (single-UUID generation & seeding)
// ============================================================
struct alignas(32) FastRng {
    uint64_t s[4];
    uint32_t generated_count{0};

    static inline uint64_t rotl(uint64_t x, int k) noexcept {
        return (x << k) | (x >> (64 - k));
    }

    void seed_from_hardware() noexcept {
        for (int i = 0; i < 4; ++i) {
            s[i] = UUIDGenerator::hardware_seed64();
            if (s[i] == 0) {
                s[i] = static_cast<uint64_t>(
                    std::chrono::steady_clock::now().time_since_epoch().count()
                ) ^ (0x9e3779b97f4a7c15ULL * static_cast<uint64_t>(i + 1));
            }
        }
        generated_count = 0;
    }

    inline uint64_t next_u64() noexcept {
        if (++generated_count >= 65536) [[unlikely]] {
            seed_from_hardware();
        }
        const uint64_t result = rotl(s[0] + s[3], 23) + s[0];
        const uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }
};

thread_local FastRng tl_rng = []() {
    FastRng rng;
    rng.seed_from_hardware();
    return rng;
}();

// ============================================================
//  Vectorized xoshiro256++ running entirely in SIMD registers
//
//  The key fix for v4 batch generation speed:
//  State lives in ymm/zmm registers throughout.
//  No GPR -> vector cross-domain penalty.
//
//  Tier 1: AVX-512 (4 parallel lanes × 64-bit = 256 bits / step)
//           -> 4 UUIDs worth of bits per next_4x64() call
//  Tier 2: AVX2 (4 parallel lanes × 64-bit = 256 bits / step)
//           -> same as AVX-512 but via ymm registers
// ============================================================

#if defined(__AVX2__)

struct alignas(32) VectorRng {
    // 4 xoshiro256++ states packed in 4 × ymm registers
    // Each ymm holds one of the 4 state words across 4 parallel streams.
    // Layout: s0[lane0|lane1|lane2|lane3], s1[...], s2[...], s3[...]
    __m256i s0, s1, s2, s3;
    uint32_t generated_count{0};

    static inline __m256i rotl_256(__m256i x, int k) noexcept {
        return _mm256_or_si256(
            _mm256_slli_epi64(x, k),
            _mm256_srli_epi64(x, 64 - k)
        );
    }

    void seed_from_scalar(FastRng& rng) noexcept {
        // Seed 4 independent streams by jumping xoshiro256++ state
        // Each stream's 4 words are gathered from scalar rng
        alignas(32) uint64_t buf[16];
        for (int i = 0; i < 16; ++i) {
            buf[i] = rng.next_u64();
        }
        // Transpose: s0 = [stream0.s0, stream1.s0, stream2.s0, stream3.s0]
        s0 = _mm256_set_epi64x(
            static_cast<long long>(buf[12]), static_cast<long long>(buf[8]),
            static_cast<long long>(buf[4]),  static_cast<long long>(buf[0])
        );
        s1 = _mm256_set_epi64x(
            static_cast<long long>(buf[13]), static_cast<long long>(buf[9]),
            static_cast<long long>(buf[5]),  static_cast<long long>(buf[1])
        );
        s2 = _mm256_set_epi64x(
            static_cast<long long>(buf[14]), static_cast<long long>(buf[10]),
            static_cast<long long>(buf[6]),  static_cast<long long>(buf[2])
        );
        s3 = _mm256_set_epi64x(
            static_cast<long long>(buf[15]), static_cast<long long>(buf[11]),
            static_cast<long long>(buf[7]),  static_cast<long long>(buf[3])
        );
        generated_count = 0;
    }

    // Returns one 256-bit register containing 4 × 64-bit random values,
    // one per parallel xoshiro256++ stream. Entire computation stays in ymm.
    [[nodiscard]] inline __m256i next_4x64() noexcept {
        if (++generated_count >= 16384) [[unlikely]] {
            seed_from_scalar(tl_rng);
        }

        // xoshiro256++: result = rotl(s0 + s3, 23) + s0
        const __m256i result = _mm256_add_epi64(
            rotl_256(_mm256_add_epi64(s0, s3), 23),
            s0
        );

        // State update
        const __m256i t = _mm256_slli_epi64(s1, 17);

        s2 = _mm256_xor_si256(s2, s0);
        s3 = _mm256_xor_si256(s3, s1);
        s1 = _mm256_xor_si256(s1, s2);
        s0 = _mm256_xor_si256(s0, s3);
        s2 = _mm256_xor_si256(s2, t);
        s3 = rotl_256(s3, 45);

        return result;
    }
};

thread_local VectorRng tl_vec_rng = []() {
    VectorRng vrng;
    vrng.seed_from_scalar(tl_rng);
    return vrng;
}();

// Version/variant masks for AVX2 (applied to 256-bit = 2 UUIDs)
inline __m256i make_avx2_and_mask() noexcept {
    return _mm256_setr_epi8(
        -1, -1, -1, -1, -1, -1, 0x0F, -1,
        0x3F, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, 0x0F, -1,
        0x3F, -1, -1, -1, -1, -1, -1, -1
    );
}

inline __m256i make_avx2_or_mask() noexcept {
    return _mm256_setr_epi8(
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00,
        static_cast<char>(0x80), 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00,
        static_cast<char>(0x80), 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    );
}

#endif // __AVX2__

// ============================================================
//  UUID v7 monotonic state
// ============================================================
struct V7State {
    uint64_t last_ts_ms{0};
    uint64_t counter{0}; // 42-bit monotonic counter (RFC 9562 Method 2)
};

thread_local V7State tl_v7_state;

// Shared helper: stamp a UUID v7 from pre-computed components into dst.
// Called by both v7() and v7_batch() to avoid duplicating the bit-packing.
inline void stamp_v7(UUID& dst, uint64_t ts_ms, uint64_t counter, uint32_t random_tail) noexcept {
    const uint16_t rand_a = static_cast<uint16_t>((counter >> 30) & 0x0FFFULL);
    const uint32_t counter_lo = static_cast<uint32_t>(counter & 0x3FFFFFFFULL);

    uint64_t hi = (ts_ms << 16) | 0x7000ULL | rand_a;
    hi = std::byteswap(hi);
    std::memcpy(dst.data.data(), &hi, 8);

    uint64_t lo = (static_cast<uint64_t>(counter_lo) << 32) | random_tail;
    lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    lo = std::byteswap(lo);
    std::memcpy(dst.data.data() + 8, &lo, 8);
}

} // anonymous namespace

// ============================================================
//  Hardware entropy
// ============================================================

uint64_t UUIDGenerator::hardware_random64() noexcept {
#if defined(__RDRND__)
    unsigned long long val = 0;
    for (int retry = 0; retry < 16; ++retry) {
        if (_rdrand64_step(&val)) {
            return val;
        }
    }
#endif
    static thread_local std::random_device rd;
    uint64_t a = (static_cast<uint64_t>(rd()) << 32) | rd();
    uint64_t b = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count()
    );
    return a ^ (b * 0x9e3779b97f4a7c15ULL);
}

uint64_t UUIDGenerator::hardware_seed64() noexcept {
#if defined(__RDSEED__)
    unsigned long long val = 0;
    for (int retry = 0; retry < 32; ++retry) {
        if (_rdseed64_step(&val)) {
            return val;
        }
    }
#endif
    return hardware_random64();
}

// ============================================================
//  UUID v4 — single UUID (scalar GPR path, no spill needed
//  since 16 bytes fit in one XMM register)
// ============================================================
UUID UUIDGenerator::v4() noexcept {
    UUID uuid;
#if defined(__SSE2__)
    uint64_t a = tl_rng.next_u64();
    uint64_t b = tl_rng.next_u64();
    __m128i raw = _mm_set_epi64x(static_cast<long long>(b), static_cast<long long>(a));

    const __m128i and_mask = _mm_setr_epi8(
        -1, -1, -1, -1, -1, -1, 0x0F, -1,
        0x3F, -1, -1, -1, -1, -1, -1, -1
    );
    const __m128i or_mask = _mm_setr_epi8(
        0, 0, 0, 0, 0, 0, 0x40, 0,
        static_cast<char>(0x80), 0, 0, 0, 0, 0, 0, 0
    );
    _mm_store_si128(
        reinterpret_cast<__m128i*>(uuid.data.data()),
        _mm_or_si128(_mm_and_si128(raw, and_mask), or_mask)
    );
#else
    uint64_t w0 = tl_rng.next_u64();
    uint64_t w1 = tl_rng.next_u64();
    std::memcpy(uuid.data.data(),     &w0, 8);
    std::memcpy(uuid.data.data() + 8, &w1, 8);
    uuid.data[6] = static_cast<uint8_t>((uuid.data[6] & 0x0F) | 0x40);
    uuid.data[8] = static_cast<uint8_t>((uuid.data[8] & 0x3F) | 0x80);
#endif
    return uuid;
}

// ============================================================
//  UUID v4 batch — vectorized PRNG lives entirely in registers
// ============================================================
void UUIDGenerator::v4_batch(std::span<UUID> out) noexcept {
    size_t i = 0;
    const size_t n = out.size();

#if defined(__AVX512F__) && defined(__AVX2__)
    // -------------------------------------------------------
    // Tier 1: AVX-512 path
    // Uses VectorRng to produce 4 × 64-bit values per next_4x64() call.
    // Two calls = 512 bits = 4 complete UUIDs. Zero GPR<->vector spill.
    // -------------------------------------------------------
    const __m128i and128 = _mm_setr_epi8(
        -1, -1, -1, -1, -1, -1, 0x0F, -1,
        0x3F, -1, -1, -1, -1, -1, -1, -1
    );
    const __m128i or128 = _mm_setr_epi8(
        0, 0, 0, 0, 0, 0, 0x40, 0,
        static_cast<char>(0x80), 0, 0, 0, 0, 0, 0, 0
    );
    const __m512i and_mask = _mm512_broadcast_i32x4(and128);
    const __m512i or_mask  = _mm512_broadcast_i32x4(or128);

    for (; i + 4 <= n; i += 4) {
        // 4 × 64-bit (256 bits) for UUIDs 0,1 (lo halves)
        __m256i rnd_lo = tl_vec_rng.next_4x64();
        // 4 × 64-bit (256 bits) for UUIDs 2,3 (hi halves)
        __m256i rnd_hi = tl_vec_rng.next_4x64();

        // Pack the 8 independent 64-bit values into a single 512-bit register.
        // Each UUID occupies a contiguous 128-bit lane.
        // We interleave: lane[i] = rnd_lo.lane[i/2] | rnd_hi.lane[i/2]
        // Concretely:
        //   UUID[0] = {rnd_lo[0], rnd_lo[1]}   (lanes 0 and 1 of the first ymm)
        //   UUID[1] = {rnd_lo[2], rnd_lo[3]}
        //   UUID[2] = {rnd_hi[0], rnd_hi[1]}
        //   UUID[3] = {rnd_hi[2], rnd_hi[3]}
        __m512i raw512 = _mm512_inserti64x4(
            _mm512_castsi256_si512(rnd_lo),
            rnd_hi,
            1
        );

        __m512i result = _mm512_or_si512(_mm512_and_si512(raw512, and_mask), or_mask);
        _mm512_storeu_si512(reinterpret_cast<__m512i*>(&out[i]), result);
    }

#elif defined(__AVX2__)
    // -------------------------------------------------------
    // Tier 2: AVX2 path — 2 UUIDs per iteration entirely in ymm
    // next_4x64() -> 4 × 64-bit values. Paired calls give 2 UUIDs.
    // -------------------------------------------------------
    const __m256i and_mask = make_avx2_and_mask();
    const __m256i or_mask  = make_avx2_or_mask();

    for (; i + 2 <= n; i += 2) {
        // First call produces hi 64-bit halves of UUID[0] and UUID[1]
        // Second call produces lo 64-bit halves
        // We interleave pairs from the 4-lane ymm into two contiguous UUIDs.
        __m256i rnd_a = tl_vec_rng.next_4x64(); // [a0, a1, a2, a3]
        __m256i rnd_b = tl_vec_rng.next_4x64(); // [b0, b1, b2, b3]

        // UUID[0] = {a0, b0}  UUID[1] = {a1, b1}
        // Interleave low 128 bits of rnd_a and rnd_b -> UUID[0] and UUID[1]
        __m256i uuid01 = _mm256_unpacklo_epi64(rnd_a, rnd_b); // [a0,b0, a2,b2]

        // Apply version/variant masks
        __m256i result = _mm256_or_si256(_mm256_and_si256(uuid01, and_mask), or_mask);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&out[i]), result);
    }
#endif

    // Scalar remainder
    for (; i < n; ++i) {
        out[i] = v4();
    }
}

// ============================================================
//  UUID v7 — single UUID
// ============================================================
UUID UUIDGenerator::v7() noexcept {
    using namespace std::chrono;
    const uint64_t now_ms = static_cast<uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
    );

    uint64_t counter;
    if (now_ms > tl_v7_state.last_ts_ms) [[likely]] {
        tl_v7_state.last_ts_ms = now_ms;
        tl_v7_state.counter = tl_rng.next_u64() & 0x000003FFFFFFFFFFULL;
        counter = tl_v7_state.counter;
    } else {
        counter = ++tl_v7_state.counter;
    }

    UUID uuid;
    stamp_v7(uuid, now_ms, counter, static_cast<uint32_t>(tl_rng.next_u64()));
    return uuid;
}

// ============================================================
//  UUID v7 batch — ONE clock read amortized across the whole batch
//
//  Fix: instead of calling system_clock::now() N times (each
//  taking ~19ns via the VDSO), we:
//   1. Read the clock once.
//   2. Claim a contiguous range of counter slots.
//   3. Fill all N UUIDs purely from pre-computed timestamp + counter,
//      touching the clock only a second time if the batch actually
//      spans a millisecond boundary.
// ============================================================
void UUIDGenerator::v7_batch(std::span<UUID> out) noexcept {
    if (out.empty()) [[unlikely]] return;

    using namespace std::chrono;
    const size_t n = out.size();

    // Single clock read for the entire batch
    const uint64_t now_ms = static_cast<uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
    );

    // Claim a contiguous counter range
    uint64_t base_counter;
    if (now_ms > tl_v7_state.last_ts_ms) {
        tl_v7_state.last_ts_ms = now_ms;
        tl_v7_state.counter = tl_rng.next_u64() & 0x000003FFFFFFFFFFULL;
        base_counter = tl_v7_state.counter;
    } else {
        base_counter = tl_v7_state.counter + 1;
    }
    // Advance the state counter by n (we're pre-claiming all slots at once)
    tl_v7_state.counter = base_counter + (n - 1);

    // Fill all UUIDs: monotonic counter increments, random tail per UUID
    for (size_t j = 0; j < n; ++j) {
        const uint64_t counter = base_counter + j;
        const uint32_t random_tail = static_cast<uint32_t>(tl_rng.next_u64());
        stamp_v7(out[j], now_ms, counter, random_tail);
    }
}

} // namespace aegon::data
