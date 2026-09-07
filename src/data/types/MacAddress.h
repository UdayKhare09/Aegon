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
 * @brief 48-bit IEEE 802 MAC address (XX:XX:XX:XX:XX:XX).
 */
class MacAddress {
public:
    std::array<uint8_t, 6> bytes_{};

    constexpr MacAddress() noexcept = default;

    constexpr explicit MacAddress(const std::array<uint8_t, 6>& bytes) noexcept
        : bytes_(bytes) {}

    constexpr explicit MacAddress(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5) noexcept
        : bytes_{b0, b1, b2, b3, b4, b5} {}

    [[nodiscard]] constexpr const uint8_t* as_bytes() const noexcept { return bytes_.data(); }

    void to_chars(char* out) const noexcept {
        for (size_t i = 0; i < 6; ++i) {
            if (i > 0) out[i * 3 - 1] = ':';
            out[i * 3] = detail::HEX_DIGITS_LOWER[bytes_[i] >> 4];
            out[i * 3 + 1] = detail::HEX_DIGITS_LOWER[bytes_[i] & 0x0F];
        }
    }

    [[nodiscard]] std::string to_string() const {
        std::string s(17, '\0');
        to_chars(s.data());
        return s;
    }

    [[nodiscard]] static std::optional<MacAddress> from_string(std::string_view s) noexcept {
        if (s.size() != 17) return std::nullopt;
        MacAddress mac;
        for (size_t i = 0; i < 6; ++i) {
            if (i > 0 && s[i * 3 - 1] != ':' && s[i * 3 - 1] != '-') return std::nullopt;
            uint8_t hi = detail::HEX_DECODE_TABLE[static_cast<uint8_t>(s[i * 3])];
            uint8_t lo = detail::HEX_DECODE_TABLE[static_cast<uint8_t>(s[i * 3 + 1])];
            if ((hi | lo) & 0xF0) return std::nullopt;
            mac.bytes_[i] = static_cast<uint8_t>((hi << 4) | lo);
        }
        return mac;
    }

    [[nodiscard]] constexpr auto operator<=>(const MacAddress& other) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const MacAddress& other) const noexcept = default;
};

inline std::ostream& operator<<(std::ostream& os, const MacAddress& mac) {
    char buf[17];
    mac.to_chars(buf);
    return os.write(buf, 17);
}

inline std::istream& operator>>(std::istream& is, MacAddress& mac) {
    std::string s;
    if (is >> s) {
        if (auto parsed = MacAddress::from_string(s)) {
            mac = *parsed;
        } else {
            is.setstate(std::ios_base::failbit);
        }
    }
    return is;
}

} // namespace aegon::data::types

namespace aegon::data {
using types::MacAddress;
}

template <>
struct std::hash<aegon::data::types::MacAddress> {
    [[nodiscard]] size_t operator()(const aegon::data::types::MacAddress& mac) const noexcept {
        uint64_t val = 0;
        for (size_t i = 0; i < 6; ++i) {
            val = (val << 8) | mac.bytes_[i];
        }
        return std::hash<uint64_t>{}(val);
    }
};

#if __has_include(<glaze/glaze.hpp>)
#include <glaze/glaze.hpp>

template <>
struct glz::meta<aegon::data::types::MacAddress> {
    static constexpr auto value = glz::custom<
        [](aegon::data::types::MacAddress& mac, const std::string_view s, glz::context& ctx) {
            auto parsed = aegon::data::types::MacAddress::from_string(s);
            if (!parsed) {
                ctx.error = glz::error_code::syntax_error;
                return;
            }
            mac = *parsed;
        },
        [](const aegon::data::types::MacAddress& mac) -> std::string {
            return mac.to_string();
        }
    >;
};
#endif
