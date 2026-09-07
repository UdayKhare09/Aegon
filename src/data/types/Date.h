#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <compare>
#include <ctime>
#include <ostream>
#include <istream>
#include "DateTime.h"

namespace aegon::data::types {

/**
 * @brief Gregorian calendar date (YYYY-MM-DD).
 *
 * Compact representation matching SQL `DATE` and ISO 8601 calendar date.
 */
class Date {
public:
    int32_t year_{1970};
    uint8_t month_{1};
    uint8_t day_{1};

    constexpr Date() noexcept = default;

    constexpr Date(int32_t y, uint8_t m, uint8_t d) noexcept
        : year_(y), month_(m), day_(d) {}

    [[nodiscard]] static Date today() noexcept {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        int64_t days = ts.tv_sec / 86400;
        int y = 0;
        unsigned m = 0, d = 0;
        detail::days_to_civil(days, y, m, d);
        return Date(y, static_cast<uint8_t>(m), static_cast<uint8_t>(d));
    }

    [[nodiscard]] constexpr int32_t year() const noexcept { return year_; }
    [[nodiscard]] constexpr uint8_t month() const noexcept { return month_; }
    [[nodiscard]] constexpr uint8_t day() const noexcept { return day_; }

    [[nodiscard]] constexpr bool is_leap_year() const noexcept {
        return (year_ % 4 == 0 && year_ % 100 != 0) || (year_ % 400 == 0);
    }

    /**
     * @brief Formats into exact 10-byte YYYY-MM-DD string.
     */
    void to_chars(char* out) const noexcept {
        detail::write_4digits(out, static_cast<unsigned>(year_ < 0 ? -year_ : year_));
        out[4] = '-';
        detail::write_2digits(out + 5, month_);
        out[7] = '-';
        detail::write_2digits(out + 8, day_);
    }

    [[nodiscard]] std::string to_string() const {
        std::string s(10, '\0');
        to_chars(s.data());
        return s;
    }

    [[nodiscard]] static std::optional<Date> from_string(std::string_view str) noexcept {
        if (str.size() != 10 || str[4] != '-' || str[7] != '-') return std::nullopt;
        unsigned y = 0, m = 0, d = 0;
        if (!detail::parse_4digits(str.substr(0, 4), y)) return std::nullopt;
        if (!detail::parse_2digits(str.substr(5, 2), m) || m == 0 || m > 12) return std::nullopt;
        if (!detail::parse_2digits(str.substr(8, 2), d) || d == 0 || d > 31) return std::nullopt;
        return Date(static_cast<int32_t>(y), static_cast<uint8_t>(m), static_cast<uint8_t>(d));
    }

    // Comparison operators
    [[nodiscard]] constexpr auto operator<=>(const Date& other) const noexcept {
        if (auto cmp = year_ <=> other.year_; cmp != 0) return cmp;
        if (auto cmp = month_ <=> other.month_; cmp != 0) return cmp;
        return day_ <=> other.day_;
    }

    [[nodiscard]] constexpr bool operator==(const Date& other) const noexcept = default;
};

inline std::ostream& operator<<(std::ostream& os, const Date& d) {
    char buf[10];
    d.to_chars(buf);
    return os.write(buf, 10);
}

inline std::istream& operator>>(std::istream& is, Date& d) {
    std::string s;
    if (is >> s) {
        if (auto parsed = Date::from_string(s)) {
            d = *parsed;
        } else {
            is.setstate(std::ios_base::failbit);
        }
    }
    return is;
}

} // namespace aegon::data::types

namespace aegon::data {
using types::Date;
}

template <>
struct std::hash<aegon::data::types::Date> {
    [[nodiscard]] size_t operator()(const aegon::data::types::Date& d) const noexcept {
        uint32_t h = (static_cast<uint32_t>(d.year()) << 9) | (static_cast<uint32_t>(d.month()) << 5) | static_cast<uint32_t>(d.day());
        return std::hash<uint32_t>{}(h);
    }
};

#if __has_include(<glaze/glaze.hpp>)
#include <glaze/glaze.hpp>

template <>
struct glz::meta<aegon::data::types::Date> {
    static constexpr auto value = glz::custom<
        [](aegon::data::types::Date& d, const std::string_view s, glz::context& ctx) {
            auto parsed = aegon::data::types::Date::from_string(s);
            if (!parsed) {
                ctx.error = glz::error_code::syntax_error;
                return;
            }
            d = *parsed;
        },
        [](const aegon::data::types::Date& d) -> std::string {
            return d.to_string();
        }
    >;
};
#endif
