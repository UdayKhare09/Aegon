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
 * @brief Wall-clock time of day (HH:MM:SS.ffffff).
 *
 * Microsecond resolution within a 24-hour cycle. Fits cleanly in SQL `TIME` / `TIME(6)`.
 */
class Time {
public:
    uint32_t micros_from_midnight_{0}; // max: 86,399,999,999 us (fits in uint64_t)
    uint64_t total_us_{0};

    constexpr Time() noexcept = default;

    constexpr Time(uint8_t h, uint8_t m, uint8_t s, uint32_t us = 0) noexcept
        : total_us_(static_cast<uint64_t>(h) * 3'600'000'000ULL +
                    static_cast<uint64_t>(m) * 60'000'000ULL +
                    static_cast<uint64_t>(s) * 1'000'000ULL + us) {}

    constexpr explicit Time(uint64_t microseconds_from_midnight) noexcept
        : total_us_(microseconds_from_midnight % 86'400'000'000ULL) {}

    [[nodiscard]] static Time now() noexcept {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        uint64_t us = (static_cast<uint64_t>(ts.tv_sec % 86400) * 1'000'000ULL) + (ts.tv_nsec / 1000);
        return Time(us);
    }

    [[nodiscard]] constexpr uint8_t hour() const noexcept {
        return static_cast<uint8_t>(total_us_ / 3'600'000'000ULL);
    }

    [[nodiscard]] constexpr uint8_t minute() const noexcept {
        return static_cast<uint8_t>((total_us_ % 3'600'000'000ULL) / 60'000'000ULL);
    }

    [[nodiscard]] constexpr uint8_t second() const noexcept {
        return static_cast<uint8_t>((total_us_ % 60'000'000ULL) / 1'000'000ULL);
    }

    [[nodiscard]] constexpr uint32_t microsecond() const noexcept {
        return static_cast<uint32_t>(total_us_ % 1'000'000ULL);
    }

    [[nodiscard]] constexpr uint64_t total_micros() const noexcept {
        return total_us_;
    }

    void to_chars(char* out, bool with_micros = true) const noexcept {
        detail::write_2digits(out, hour());
        out[2] = ':';
        detail::write_2digits(out + 3, minute());
        out[5] = ':';
        detail::write_2digits(out + 6, second());
        if (with_micros) {
            out[8] = '.';
            detail::write_6digits(out + 9, microsecond());
        }
    }

    [[nodiscard]] std::string to_string(bool with_micros = true) const {
        std::string s(with_micros ? 15 : 8, '\0');
        to_chars(s.data(), with_micros);
        return s;
    }

    [[nodiscard]] static std::optional<Time> from_string(std::string_view str) noexcept {
        if (str.size() < 8 || str[2] != ':' || str[5] != ':') return std::nullopt;
        unsigned h = 0, m = 0, s = 0;
        if (!detail::parse_2digits(str.substr(0, 2), h) || h > 23) return std::nullopt;
        if (!detail::parse_2digits(str.substr(3, 2), m) || m > 59) return std::nullopt;
        if (!detail::parse_2digits(str.substr(6, 2), s) || s > 59) return std::nullopt;

        uint32_t us = 0;
        if (str.size() > 8 && str[8] == '.') {
            std::string_view frac = str.substr(9);
            for (size_t i = 0; i < frac.size() && i < 6; ++i) {
                if (frac[i] < '0' || frac[i] > '9') break;
                us = us * 10 + (frac[i] - '0');
            }
            for (size_t i = frac.size(); i < 6; ++i) {
                us *= 10;
            }
        }

        return Time(static_cast<uint8_t>(h), static_cast<uint8_t>(m), static_cast<uint8_t>(s), us);
    }

    [[nodiscard]] constexpr auto operator<=>(const Time& other) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const Time& other) const noexcept = default;
};

inline std::ostream& operator<<(std::ostream& os, const Time& t) {
    char buf[15];
    t.to_chars(buf, true);
    return os.write(buf, 15);
}

inline std::istream& operator>>(std::istream& is, Time& t) {
    std::string s;
    if (is >> s) {
        if (auto parsed = Time::from_string(s)) {
            t = *parsed;
        } else {
            is.setstate(std::ios_base::failbit);
        }
    }
    return is;
}

} // namespace aegon::data::types

namespace aegon::data {
using types::Time;
}

template <>
struct std::hash<aegon::data::types::Time> {
    [[nodiscard]] size_t operator()(const aegon::data::types::Time& t) const noexcept {
        return std::hash<uint64_t>{}(t.total_micros());
    }
};

#if __has_include(<glaze/glaze.hpp>)
#include <glaze/glaze.hpp>

template <>
struct glz::meta<aegon::data::types::Time> {
    static constexpr auto value = glz::custom<
        [](aegon::data::types::Time& t, const std::string_view s, glz::context& ctx) {
            auto parsed = aegon::data::types::Time::from_string(s);
            if (!parsed) {
                ctx.error = glz::error_code::syntax_error;
                return;
            }
            t = *parsed;
        },
        [](const aegon::data::types::Time& t) -> std::string {
            return t.to_string();
        }
    >;
};
#endif
