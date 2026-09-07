#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <string_view>
#include <optional>
#include <compare>
#include <ostream>
#include <istream>
#include "Hex.h"

namespace aegon::data::types {

/**
 * @brief 256-bit (32-byte) cryptographic hash or authentication token.
 *
 * Implements constant-time equality comparisons to prevent timing side-channel attacks,
 * fast 64-char hex string conversion, and Glaze JSON DTO integration.
 */
class alignas(16) Hash256 {
public:
    std::array<uint8_t, 32> bytes_{};

    constexpr Hash256() noexcept = default;

    constexpr explicit Hash256(const std::array<uint8_t, 32>& bytes) noexcept
        : bytes_(bytes) {}

    constexpr explicit Hash256(const uint8_t* ptr) noexcept {
        for (size_t i = 0; i < 32; ++i) {
            bytes_[i] = ptr[i];
        }
    }

    [[nodiscard]] constexpr const uint8_t* data() const noexcept { return bytes_.data(); }
    [[nodiscard]] constexpr uint8_t* data() noexcept { return bytes_.data(); }
    [[nodiscard]] constexpr size_t size() const noexcept { return 32; }

    void to_hex(char* out) const noexcept {
        for (size_t i = 0; i < 32; ++i) {
            out[i * 2]     = detail::HEX_DIGITS_LOWER[bytes_[i] >> 4];
            out[i * 2 + 1] = detail::HEX_DIGITS_LOWER[bytes_[i] & 0x0F];
        }
    }

    [[nodiscard]] std::string to_hex() const {
        std::string s(64, '\0');
        to_hex(s.data());
        return s;
    }

    [[nodiscard]] std::string to_string() const {
        return to_hex();
    }

    [[nodiscard]] static std::optional<Hash256> from_hex(std::string_view hex) noexcept {
        if (hex.size() != 64) return std::nullopt;
        Hash256 h;
        for (size_t i = 0; i < 32; ++i) {
            uint8_t hi = detail::HEX_DECODE_TABLE[static_cast<uint8_t>(hex[i * 2])];
            uint8_t lo = detail::HEX_DECODE_TABLE[static_cast<uint8_t>(hex[i * 2 + 1])];
            if ((hi | lo) & 0xF0) return std::nullopt;
            h.bytes_[i] = static_cast<uint8_t>((hi << 4) | lo);
        }
        return h;
    }

    [[nodiscard]] static std::optional<Hash256> from_string(std::string_view s) noexcept {
        return from_hex(s);
    }

    /**
     * @brief Constant-time equality comparison to protect against timing attacks.
     */
    [[nodiscard]] bool operator==(const Hash256& other) const noexcept {
        uint8_t diff = 0;
        for (size_t i = 0; i < 32; ++i) {
            diff |= (bytes_[i] ^ other.bytes_[i]);
        }
        return diff == 0;
    }

    [[nodiscard]] auto operator<=>(const Hash256& other) const noexcept {
        return bytes_ <=> other.bytes_;
    }
};

inline std::ostream& operator<<(std::ostream& os, const Hash256& h) {
    char buf[64];
    h.to_hex(buf);
    return os.write(buf, 64);
}

inline std::istream& operator>>(std::istream& is, Hash256& h) {
    std::string s;
    if (is >> s) {
        if (auto parsed = Hash256::from_hex(s)) {
            h = *parsed;
        } else {
            is.setstate(std::ios_base::failbit);
        }
    }
    return is;
}

} // namespace aegon::data::types

namespace aegon::data {
using types::Hash256;
}

template <>
struct std::hash<aegon::data::types::Hash256> {
    [[nodiscard]] size_t operator()(const aegon::data::types::Hash256& h) const noexcept {
        const auto* w = reinterpret_cast<const uint64_t*>(h.data());
        return std::hash<uint64_t>{}(w[0] ^ w[1] ^ w[2] ^ w[3]);
    }
};

#if __has_include(<glaze/glaze.hpp>)
#include <glaze/glaze.hpp>

template <>
struct glz::meta<aegon::data::types::Hash256> {
    static constexpr auto value = glz::custom<
        [](aegon::data::types::Hash256& h, const std::string_view s, glz::context& ctx) {
            auto parsed = aegon::data::types::Hash256::from_hex(s);
            if (!parsed) {
                ctx.error = glz::error_code::syntax_error;
                return;
            }
            h = *parsed;
        },
        [](const aegon::data::types::Hash256& h) -> std::string {
            return h.to_hex();
        }
    >;
};
#endif
