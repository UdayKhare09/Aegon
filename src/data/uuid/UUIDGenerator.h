#pragma once

#include "UUID.h"
#include <span>
#include <cstdint>

namespace aegon::data {

/**
 * @brief Ultra-high performance hardware-accelerated UUID generator for v4 and v7.
 *
 * Utilizes:
 * - AMD Zen 4 RDRAND and RDSEED CPU instructions for true hardware entropy.
 * - Hardware AES-NI / VAES-CTR stream cipher engine for ultra-high throughput bulk generation.
 * - RFC 9562 compliant monotonic sequence counter for UUID v7.
 * - AVX-512 / AVX2 batch generation vector pipelines.
 */
class UUIDGenerator {
public:
    /**
     * @brief Generates a cryptographically strong UUID v4 (random).
     */
    [[nodiscard]] static UUID v4() noexcept;

    /**
     * @brief Generates a time-ordered UUID v7 (RFC 9562) with guaranteed monotonic ordering.
     */
    [[nodiscard]] static UUID v7() noexcept;

    /**
     * @brief Batch generation of UUID v4 using AVX-512 vector pipelines.
     * Generates multiple UUIDs concurrently per CPU instruction.
     */
    static void v4_batch(std::span<UUID> out) noexcept;

    /**
     * @brief Batch generation of strictly monotonic UUID v7 IDs.
     */
    static void v7_batch(std::span<UUID> out) noexcept;

    /**
     * @brief Direct hardware random 64-bit value using CPU RDRAND instruction.
     */
    [[nodiscard]] static uint64_t hardware_random64() noexcept;

    /**
     * @brief Direct hardware entropy seed using CPU RDSEED instruction.
     */
    [[nodiscard]] static uint64_t hardware_seed64() noexcept;
};

} // namespace aegon::data
