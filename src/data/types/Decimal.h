#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <compare>
#include <cmath>
#include <ostream>
#include <istream>
#include <concepts>

namespace aegon::data::types {

namespace detail {

consteval __int128_t compute_pow10(uint8_t n) noexcept {
    __int128_t r = 1;
    for (uint8_t i = 0; i < n; ++i) r *= 10;
    return r;
}

} // namespace detail

/**
 * @brief High-precision fixed-point decimal type with zero floating-point representation error.
 *
 * Implements exact 128-bit signed integer scaling matching SQL `DECIMAL(Precision, Scale)`
 * and Java `BigDecimal`. Completely eliminates binary floating-point roundoff bugs (e.g. 0.1 + 0.2).
 */
template <uint8_t Precision = 18, uint8_t Scale = 4>
class Decimal {
    static_assert(Scale <= Precision, "Scale cannot exceed Precision");
    static_assert(Precision <= 38, "Max 128-bit decimal precision is 38 digits");

public:
    static constexpr uint8_t PRECISION = Precision;
    static constexpr uint8_t SCALE = Scale;
    static constexpr __int128_t SCALE_FACTOR = detail::compute_pow10(Scale);

    __int128_t raw_{0}; // Scaled value: actual_value * 10^Scale

    constexpr Decimal() noexcept = default;

    constexpr explicit Decimal(__int128_t raw, bool /*internal_raw*/) noexcept
        : raw_(raw) {}

    template <std::integral Int>
    constexpr Decimal(Int val) noexcept
        : raw_(static_cast<__int128_t>(val) * SCALE_FACTOR) {}

    constexpr explicit Decimal(double val) noexcept
        : raw_(static_cast<__int128_t>(std::round(val * static_cast<double>(SCALE_FACTOR)))) {}

    explicit Decimal(std::string_view s) {
        if (auto parsed = from_string(s)) {
            *this = *parsed;
        } else {
            raw_ = 0;
        }
    }

    [[nodiscard]] constexpr __int128_t raw_value() const noexcept { return raw_; }
    [[nodiscard]] constexpr bool is_zero() const noexcept { return raw_ == 0; }
    [[nodiscard]] constexpr bool is_negative() const noexcept { return raw_ < 0; }
    [[nodiscard]] constexpr bool is_positive() const noexcept { return raw_ > 0; }

    [[nodiscard]] constexpr double to_double() const noexcept {
        return static_cast<double>(raw_) / static_cast<double>(SCALE_FACTOR);
    }

    [[nodiscard]] constexpr int64_t to_int64() const noexcept {
        return static_cast<int64_t>(raw_ / SCALE_FACTOR);
    }

    [[nodiscard]] std::string to_string() const {
        if (raw_ == 0) {
            std::string res = "0";
            if constexpr (Scale > 0) {
                res.push_back('.');
                res.append(Scale, '0');
            }
            return res;
        }

        bool neg = raw_ < 0;
        __int128_t val = neg ? -raw_ : raw_;

        std::string digits;
        while (val > 0) {
            digits.push_back(static_cast<char>('0' + (val % 10)));
            val /= 10;
        }

        while (digits.size() <= Scale) {
            digits.push_back('0');
        }

        std::string out;
        if (neg) out.push_back('-');

        size_t int_digits = digits.size() - Scale;
        for (size_t i = 0; i < int_digits; ++i) {
            out.push_back(digits[digits.size() - 1 - i]);
        }

        if constexpr (Scale > 0) {
            out.push_back('.');
            for (size_t i = int_digits; i < digits.size(); ++i) {
                out.push_back(digits[digits.size() - 1 - i]);
            }
        }

        return out;
    }

    [[nodiscard]] static std::optional<Decimal> from_string(std::string_view s) noexcept {
        if (s.empty()) return std::nullopt;

        bool neg = false;
        size_t idx = 0;
        if (s[0] == '-') {
            neg = true;
            idx++;
        } else if (s[0] == '+') {
            idx++;
        }

        if (idx >= s.size()) return std::nullopt;

        __int128_t integer_part = 0;
        bool has_integer = false;

        while (idx < s.size() && s[idx] >= '0' && s[idx] <= '9') {
            has_integer = true;
            integer_part = integer_part * 10 + (s[idx] - '0');
            idx++;
        }

        __int128_t frac_part = 0;
        uint8_t frac_digits = 0;

        if (idx < s.size() && s[idx] == '.') {
            idx++;
            while (idx < s.size() && s[idx] >= '0' && s[idx] <= '9') {
                if (frac_digits < Scale) {
                    frac_part = frac_part * 10 + (s[idx] - '0');
                    frac_digits++;
                }
                idx++;
            }
        }

        if (!has_integer && frac_digits == 0) return std::nullopt;
        if (idx < s.size()) return std::nullopt; // Extra invalid characters

        while (frac_digits < Scale) {
            frac_part *= 10;
            frac_digits++;
        }

        __int128_t total = integer_part * SCALE_FACTOR + frac_part;
        return Decimal(neg ? -total : total, true);
    }

    // Arithmetic operators
    [[nodiscard]] constexpr Decimal operator+(const Decimal& o) const noexcept {
        return Decimal(raw_ + o.raw_, true);
    }

    [[nodiscard]] constexpr Decimal operator-(const Decimal& o) const noexcept {
        return Decimal(raw_ - o.raw_, true);
    }

    [[nodiscard]] constexpr Decimal operator-() const noexcept {
        return Decimal(-raw_, true);
    }

    [[nodiscard]] constexpr Decimal operator*(const Decimal& o) const noexcept {
        return Decimal((raw_ * o.raw_) / SCALE_FACTOR, true);
    }

    [[nodiscard]] constexpr Decimal operator/(const Decimal& o) const noexcept {
        return Decimal((raw_ * SCALE_FACTOR) / o.raw_, true);
    }

    constexpr Decimal& operator+=(const Decimal& o) noexcept { raw_ += o.raw_; return *this; }
    constexpr Decimal& operator-=(const Decimal& o) noexcept { raw_ -= o.raw_; return *this; }
    constexpr Decimal& operator*=(const Decimal& o) noexcept { raw_ = (raw_ * o.raw_) / SCALE_FACTOR; return *this; }
    constexpr Decimal& operator/=(const Decimal& o) noexcept { raw_ = (raw_ * SCALE_FACTOR) / o.raw_; return *this; }

    // Comparisons
    [[nodiscard]] constexpr auto operator<=>(const Decimal& other) const noexcept {
        return raw_ <=> other.raw_;
    }

    [[nodiscard]] constexpr bool operator==(const Decimal& other) const noexcept {
        return raw_ == other.raw_;
    }
};

// Convenience alias for common 4-decimal currency / financial scale
using Decimal128 = Decimal<18, 4>;

template <uint8_t P, uint8_t S>
inline std::ostream& operator<<(std::ostream& os, const Decimal<P, S>& d) {
    return os << d.to_string();
}

template <uint8_t P, uint8_t S>
inline std::istream& operator>>(std::istream& is, Decimal<P, S>& d) {
    std::string s;
    if (is >> s) {
        if (auto parsed = Decimal<P, S>::from_string(s)) {
            d = *parsed;
        } else {
            is.setstate(std::ios_base::failbit);
        }
    }
    return is;
}

} // namespace aegon::data::types

namespace aegon::data {
using types::Decimal;
using types::Decimal128;
}

template <uint8_t P, uint8_t S>
struct std::hash<aegon::data::types::Decimal<P, S>> {
    [[nodiscard]] size_t operator()(const aegon::data::types::Decimal<P, S>& d) const noexcept {
        auto raw = d.raw_value();
        uint64_t lo = static_cast<uint64_t>(raw);
        uint64_t hi = static_cast<uint64_t>(raw >> 64);
        return std::hash<uint64_t>{}(lo ^ (hi * 0x9e3779b97f4a7c15ULL));
    }
};

#if __has_include(<glaze/glaze.hpp>)
#include <glaze/glaze.hpp>

template <uint8_t P, uint8_t S>
struct glz::meta<aegon::data::types::Decimal<P, S>> {
    static constexpr auto value = glz::custom<
        [](aegon::data::types::Decimal<P, S>& d, const std::string_view s, glz::context& ctx) {
            auto parsed = aegon::data::types::Decimal<P, S>::from_string(s);
            if (!parsed) {
                ctx.error = glz::error_code::syntax_error;
                return;
            }
            d = *parsed;
        },
        [](const aegon::data::types::Decimal<P, S>& d) -> std::string {
            return d.to_string();
        }
    >;
};
#endif
