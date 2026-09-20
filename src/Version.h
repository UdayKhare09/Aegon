#pragma once

#include <cstdint>
#include <string_view>

/**
 * @file Version.h
 * @brief Canonical compile-time and runtime version definitions for Aegon.
 */

#define AEGON_VERSION_MAJOR 0
#define AEGON_VERSION_MINOR 1
#define AEGON_VERSION_PATCH 0
#define AEGON_VERSION_PRERELEASE "a2"
#define AEGON_VERSION_STRING "0.1.a2"

/**
 * @brief Helper macro for compile-time version checks:
 * #if AEGON_VERSION_HEX >= AEGON_VERSION_CHECK(0, 1, 0)
 */
#define AEGON_VERSION_CHECK(major, minor, patch) \
    (((major) << 16) | ((minor) << 8) | (patch))

#define AEGON_VERSION_HEX \
    AEGON_VERSION_CHECK(AEGON_VERSION_MAJOR, AEGON_VERSION_MINOR, AEGON_VERSION_PATCH)

namespace aegon {

/**
 * @brief Strongly typed version descriptor for modern C++20/C++26 applications.
 */
struct VersionInfo {
    uint32_t major{AEGON_VERSION_MAJOR};
    uint32_t minor{AEGON_VERSION_MINOR};
    uint32_t patch{AEGON_VERSION_PATCH};
    std::string_view prerelease{AEGON_VERSION_PRERELEASE};
    std::string_view string{AEGON_VERSION_STRING};
    uint32_t hex{AEGON_VERSION_HEX};

    [[nodiscard]] constexpr bool is_at_least(uint32_t maj, uint32_t min, uint32_t pat = 0) const noexcept {
        return hex >= AEGON_VERSION_CHECK(maj, min, pat);
    }
};

inline constexpr VersionInfo version{};

} // namespace aegon
