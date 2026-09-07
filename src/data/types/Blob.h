#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <optional>
#include <span>
#include <compare>
#include <ostream>
#include <istream>

namespace aegon::data::types {

namespace detail {

inline constexpr char BASE64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

inline std::string base64_encode(const uint8_t* bytes, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i < len) {
        uint32_t b0 = bytes[i++];
        uint32_t b1 = (i < len) ? bytes[i++] : 0;
        uint32_t b2 = (i < len) ? bytes[i++] : 0;

        uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(BASE64_CHARS[(triple >> 18) & 0x3F]);
        out.push_back(BASE64_CHARS[(triple >> 12) & 0x3F]);
        out.push_back((i > len + 1) ? '=' : BASE64_CHARS[(triple >> 6) & 0x3F]);
        out.push_back((i > len) ? '=' : BASE64_CHARS[triple & 0x3F]);
    }
    return out;
}

inline std::optional<std::vector<uint8_t>> base64_decode(std::string_view in) {
    if (in.size() % 4 != 0) return std::nullopt;

    auto decode_char = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        if (c == '=') return -1;
        return -2; // invalid
    };

    std::vector<uint8_t> out;
    out.reserve((in.size() / 4) * 3);

    for (size_t i = 0; i < in.size(); i += 4) {
        int c0 = decode_char(in[i]);
        int c1 = decode_char(in[i + 1]);
        int c2 = decode_char(in[i + 2]);
        int c3 = decode_char(in[i + 3]);

        if (c0 < 0 || c1 < 0 || c2 == -2 || c3 == -2) return std::nullopt;

        uint32_t triple = (static_cast<uint32_t>(c0) << 18) | (static_cast<uint32_t>(c1) << 12);
        out.push_back(static_cast<uint8_t>((triple >> 16) & 0xFF));

        if (c2 != -1) {
            triple |= (static_cast<uint32_t>(c2) << 6);
            out.push_back(static_cast<uint8_t>((triple >> 8) & 0xFF));
        }
        if (c3 != -1 && c2 != -1) {
            triple |= static_cast<uint32_t>(c3);
            out.push_back(static_cast<uint8_t>(triple & 0xFF));
        }
    }
    return out;
}

} // namespace detail

/**
 * @brief Binary Large Object (BLOB / BYTEA) wrapper.
 *
 * Automatically converts to/from Base64 in JSON DTOs and handles raw bytes in SQL/Redis.
 */
class Blob {
public:
    std::vector<uint8_t> bytes_;

    Blob() = default;

    explicit Blob(std::vector<uint8_t> data) : bytes_(std::move(data)) {}
    explicit Blob(std::span<const uint8_t> data) : bytes_(data.begin(), data.end()) {}
    explicit Blob(const uint8_t* ptr, size_t len) : bytes_(ptr, ptr + len) {}

    [[nodiscard]] const uint8_t* data() const noexcept { return bytes_.data(); }
    [[nodiscard]] uint8_t* data() noexcept { return bytes_.data(); }
    [[nodiscard]] size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] bool empty() const noexcept { return bytes_.empty(); }

    [[nodiscard]] const std::vector<uint8_t>& bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::vector<uint8_t>& bytes() noexcept { return bytes_; }

    [[nodiscard]] std::string to_base64() const {
        return detail::base64_encode(bytes_.data(), bytes_.size());
    }

    [[nodiscard]] static std::optional<Blob> from_base64(std::string_view b64) {
        auto dec = detail::base64_decode(b64);
        if (!dec) return std::nullopt;
        return Blob(std::move(*dec));
    }

    [[nodiscard]] auto operator<=>(const Blob& other) const noexcept = default;
    [[nodiscard]] bool operator==(const Blob& other) const noexcept = default;
};

inline std::ostream& operator<<(std::ostream& os, const Blob& b) {
    return os << b.to_base64();
}

} // namespace aegon::data::types

namespace aegon::data {
using types::Blob;
}

template <>
struct std::hash<aegon::data::types::Blob> {
    [[nodiscard]] size_t operator()(const aegon::data::types::Blob& b) const noexcept {
        size_t h = 0;
        for (uint8_t byte : b.bytes_) {
            h = (h * 131) + byte;
        }
        return h;
    }
};

#if __has_include(<glaze/glaze.hpp>)
#include <glaze/glaze.hpp>

template <>
struct glz::meta<aegon::data::types::Blob> {
    static constexpr auto value = glz::custom<
        [](aegon::data::types::Blob& b, const std::string_view s, glz::context& ctx) {
            auto parsed = aegon::data::types::Blob::from_base64(s);
            if (!parsed) {
                ctx.error = glz::error_code::syntax_error;
                return;
            }
            b = std::move(*parsed);
        },
        [](const aegon::data::types::Blob& b) -> std::string {
            return b.to_base64();
        }
    >;
};
#endif
