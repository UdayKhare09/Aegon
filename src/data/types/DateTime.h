#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <compare>
#include <chrono>
#include <ctime>
#include <ostream>
#include <istream>
#include <format>

namespace aegon::data::types {

namespace detail {

// Fast, exact Gregorian calendar conversions (Howard Hinnant algorithms)
inline constexpr void days_to_civil(int64_t days, int& y, unsigned& m, unsigned& d) noexcept {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y_ = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int>(y_ + (m <= 2 ? 1 : 0));
}

inline constexpr int64_t civil_to_days(int y, unsigned m, unsigned d) noexcept {
    y -= m <= 2 ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline void write_2digits(char* buf, unsigned val) noexcept {
    buf[0] = static_cast<char>('0' + (val / 10));
    buf[1] = static_cast<char>('0' + (val % 10));
}

inline void write_4digits(char* buf, unsigned val) noexcept {
    write_2digits(buf, val / 100);
    write_2digits(buf + 2, val % 100);
}

inline void write_6digits(char* buf, unsigned val) noexcept {
    write_2digits(buf, val / 10000);
    write_4digits(buf + 2, val % 10000);
}

inline bool parse_2digits(std::string_view s, unsigned& val) noexcept {
    if (s.size() < 2 || s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9') return false;
    val = static_cast<unsigned>((s[0] - '0') * 10 + (s[1] - '0'));
    return true;
}

inline bool parse_4digits(std::string_view s, unsigned& val) noexcept {
    unsigned hi = 0, lo = 0;
    if (!parse_2digits(s, hi) || !parse_2digits(s.substr(2), lo)) return false;
    val = hi * 100 + lo;
    return true;
}

} // namespace detail

/**
 * @brief High-performance, 64-bit UTC timestamp with microsecond precision.
 *
 * Provides zero-allocation ISO 8601 formatting (`YYYY-MM-DDTHH:MM:SS.ffffffZ`),
 * fast parsing across timezones, SQL wire format support, and Glaze JSON reflection.
 */
class DateTime {
public:
    // Microseconds since Unix epoch 1970-01-01T00:00:00.000000Z
    int64_t micros_{0};

    constexpr DateTime() noexcept = default;

    constexpr explicit DateTime(int64_t microseconds_since_epoch) noexcept
        : micros_(microseconds_since_epoch) {}

    // Factory methods
    [[nodiscard]] static DateTime now() noexcept {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return DateTime(static_cast<int64_t>(ts.tv_sec) * 1'000'000LL + ts.tv_nsec / 1000);
    }

    [[nodiscard]] static DateTime utc_now() noexcept {
        return now();
    }

    [[nodiscard]] static constexpr DateTime from_epoch_micros(int64_t us) noexcept {
        return DateTime(us);
    }

    [[nodiscard]] static constexpr DateTime from_epoch_millis(int64_t ms) noexcept {
        return DateTime(ms * 1000);
    }

    [[nodiscard]] static constexpr DateTime from_epoch_seconds(int64_t s) noexcept {
        return DateTime(s * 1'000'000LL);
    }

    // Accessors
    [[nodiscard]] constexpr int64_t epoch_micros() const noexcept { return micros_; }
    [[nodiscard]] constexpr int64_t epoch_millis() const noexcept { return micros_ / 1000; }
    [[nodiscard]] constexpr int64_t epoch_seconds() const noexcept { return micros_ / 1'000'000LL; }

    [[nodiscard]] constexpr bool is_epoch() const noexcept { return micros_ == 0; }

    /**
     * @brief Formats into exact 27-byte ISO 8601 UTC string (YYYY-MM-DDTHH:MM:SS.ffffffZ).
     * Output buffer must have at least 27 bytes available.
     */
    void to_iso8601(char* out) const noexcept {
        int64_t total_sec = micros_ / 1'000'000LL;
        int64_t rem_us = micros_ % 1'000'000LL;
        if (rem_us < 0) {
            total_sec -= 1;
            rem_us += 1'000'000LL;
        }

        int64_t days = total_sec / 86400;
        int rem_sec = static_cast<int>(total_sec % 86400);
        if (rem_sec < 0) {
            days -= 1;
            rem_sec += 86400;
        }

        int y = 0;
        unsigned m = 0, d = 0;
        detail::days_to_civil(days, y, m, d);

        unsigned hour = static_cast<unsigned>(rem_sec / 3600);
        unsigned min = static_cast<unsigned>((rem_sec % 3600) / 60);
        unsigned sec = static_cast<unsigned>(rem_sec % 60);

        detail::write_4digits(out, static_cast<unsigned>(y));
        out[4] = '-';
        detail::write_2digits(out + 5, m);
        out[7] = '-';
        detail::write_2digits(out + 8, d);
        out[10] = 'T';
        detail::write_2digits(out + 11, hour);
        out[13] = ':';
        detail::write_2digits(out + 14, min);
        out[16] = ':';
        detail::write_2digits(out + 17, sec);
        out[19] = '.';
        detail::write_6digits(out + 20, static_cast<unsigned>(rem_us));
        out[26] = 'Z';
    }

    [[nodiscard]] std::string to_string() const {
        std::string s(27, '\0');
        to_iso8601(s.data());
        return s;
    }

    [[nodiscard]] std::string to_iso8601() const {
        return to_string();
    }

    /**
     * @brief Parses an ISO 8601 string (e.g. 2026-09-07T15:05:48.123456Z or 2026-09-07 15:05:48+05:30)
     */
    [[nodiscard]] static std::optional<DateTime> from_string(std::string_view str) noexcept {
        if (str.size() < 19) return std::nullopt; // Minimum: YYYY-MM-DDTHH:MM:SS

        unsigned y = 0, m = 0, d = 0, hr = 0, min = 0, sec = 0;
        if (!detail::parse_4digits(str.substr(0, 4), y)) return std::nullopt;
        if (str[4] != '-') return std::nullopt;
        if (!detail::parse_2digits(str.substr(5, 2), m) || m == 0 || m > 12) return std::nullopt;
        if (str[7] != '-') return std::nullopt;
        if (!detail::parse_2digits(str.substr(8, 2), d) || d == 0 || d > 31) return std::nullopt;

        char sep = str[10];
        if (sep != 'T' && sep != ' ' && sep != 't') return std::nullopt;

        if (!detail::parse_2digits(str.substr(11, 2), hr) || hr > 23) return std::nullopt;
        if (str[13] != ':') return std::nullopt;
        if (!detail::parse_2digits(str.substr(14, 2), min) || min > 59) return std::nullopt;
        if (str[16] != ':') return std::nullopt;
        if (!detail::parse_2digits(str.substr(17, 2), sec) || sec > 59) return std::nullopt;

        size_t idx = 19;
        uint32_t us = 0;

        // Microseconds fraction (.123456)
        if (idx < str.size() && str[idx] == '.') {
            idx++;
            size_t frac_start = idx;
            while (idx < str.size() && str[idx] >= '0' && str[idx] <= '9') {
                idx++;
            }
            size_t frac_len = idx - frac_start;
            if (frac_len > 0) {
                uint32_t val = 0;
                for (size_t i = 0; i < frac_len && i < 6; ++i) {
                    val = val * 10 + (str[frac_start + i] - '0');
                }
                for (size_t i = frac_len; i < 6; ++i) {
                    val *= 10;
                }
                us = val;
            }
        }

        int64_t days = detail::civil_to_days(static_cast<int>(y), m, d);
        int64_t total_sec = days * 86400 + hr * 3600 + min * 60 + sec;

        // Parse optional timezone offset (Z, +HH:MM, -HH:MM, +HHMM)
        if (idx < str.size()) {
            char tz = str[idx];
            if (tz == 'Z' || tz == 'z') {
                // UTC
            } else if (tz == '+' || tz == '-') {
                idx++;
                unsigned tz_hr = 0, tz_min = 0;
                if (!detail::parse_2digits(str.substr(idx, 2), tz_hr)) return std::nullopt;
                idx += 2;
                if (idx < str.size() && str[idx] == ':') idx++;
                if (idx + 1 < str.size()) {
                    if (!detail::parse_2digits(str.substr(idx, 2), tz_min)) return std::nullopt;
                }
                int64_t offset_sec = tz_hr * 3600 + tz_min * 60;
                if (tz == '+') {
                    total_sec -= offset_sec; // Convert local to UTC
                } else {
                    total_sec += offset_sec;
                }
            }
        }

        return DateTime(total_sec * 1'000'000LL + us);
    }

    // Comparison operators
    [[nodiscard]] constexpr auto operator<=>(const DateTime& other) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const DateTime& other) const noexcept = default;

    // Arithmetic operators
    template <typename Rep, typename Period>
    constexpr DateTime operator+(const std::chrono::duration<Rep, Period>& d) const noexcept {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(d).count();
        return DateTime(micros_ + us);
    }

    template <typename Rep, typename Period>
    constexpr DateTime operator-(const std::chrono::duration<Rep, Period>& d) const noexcept {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(d).count();
        return DateTime(micros_ - us);
    }

    constexpr std::chrono::microseconds operator-(const DateTime& other) const noexcept {
        return std::chrono::microseconds(micros_ - other.micros_);
    }
};

// Stream operators
inline std::ostream& operator<<(std::ostream& os, const DateTime& dt) {
    char buf[27];
    dt.to_iso8601(buf);
    return os.write(buf, 27);
}

inline std::istream& operator>>(std::istream& is, DateTime& dt) {
    std::string s;
    if (is >> s) {
        if (auto parsed = DateTime::from_string(s)) {
            dt = *parsed;
        } else {
            is.setstate(std::ios_base::failbit);
        }
    }
    return is;
}

} // namespace aegon::data::types

namespace aegon::data {
using types::DateTime;
}

template <>
struct std::hash<aegon::data::types::DateTime> {
    [[nodiscard]] size_t operator()(const aegon::data::types::DateTime& dt) const noexcept {
        return std::hash<int64_t>{}(dt.epoch_micros());
    }
};

#if __has_include(<glaze/glaze.hpp>)
#include <glaze/glaze.hpp>

template <>
struct glz::meta<aegon::data::types::DateTime> {
    static constexpr auto value = glz::custom<
        [](aegon::data::types::DateTime& dt, const std::string_view s, glz::context& ctx) {
            auto parsed = aegon::data::types::DateTime::from_string(s);
            if (!parsed) {
                ctx.error = glz::error_code::syntax_error;
                return;
            }
            dt = *parsed;
        },
        [](const aegon::data::types::DateTime& dt) -> std::string {
            return dt.to_string();
        }
    >;
};
#endif
