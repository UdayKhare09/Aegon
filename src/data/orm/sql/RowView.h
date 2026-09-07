#pragma once

#include "data/types/Types.h"
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <charconv>
#include <stdexcept>
#include <type_traits>

namespace aegon::data::orm::sql {

template <typename T>
T parse_field_value(std::string_view s);

class RowView {
public:
    virtual ~RowView() = default;

    [[nodiscard]] virtual size_t column_count() const noexcept = 0;
    [[nodiscard]] virtual bool is_null(size_t col_idx) const = 0;
    [[nodiscard]] virtual std::string_view get_raw(size_t col_idx) const = 0;

    template <typename T>
    [[nodiscard]] T get(size_t col_idx) const {
        if constexpr (requires { typename T::value_type; }) {
            // Check for std::optional
            if constexpr (std::is_same_v<T, std::optional<typename T::value_type>>) {
                if (is_null(col_idx)) {
                    return std::nullopt;
                }
                return get<typename T::value_type>(col_idx);
            }
        }

        if (is_null(col_idx)) {
            return T{};
        }

        return parse_field_value<T>(get_raw(col_idx));
    }
};

template <typename T>
inline T parse_field_value(std::string_view s) {
    if constexpr (std::is_same_v<T, std::string>) {
        return std::string(s);
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        return s;
    } else if constexpr (std::is_same_v<T, bool>) {
        return (s == "1" || s == "true" || s == "TRUE" || s == "t" || s == "T");
    } else if constexpr (std::is_integral_v<T>) {
        T val{};
        std::from_chars(s.data(), s.data() + s.size(), val);
        return val;
    } else if constexpr (std::is_floating_point_v<T>) {
        T val{};
        std::from_chars(s.data(), s.data() + s.size(), val);
        return val;
    } else if constexpr (std::is_same_v<T, types::UUID>) {
        auto opt = types::UUID::from_string(s);
        return opt ? *opt : types::UUID{};
    } else if constexpr (std::is_same_v<T, types::DateTime>) {
        auto opt = types::DateTime::from_iso8601(s);
        return opt ? *opt : types::DateTime{};
    } else if constexpr (std::is_same_v<T, types::Date>) {
        auto opt = types::Date::from_string(s);
        return opt ? *opt : types::Date{};
    } else if constexpr (std::is_same_v<T, types::Time>) {
        auto opt = types::Time::from_string(s);
        return opt ? *opt : types::Time{};
    } else if constexpr (requires { T::from_string(s); }) {
        auto opt = T::from_string(s);
        if constexpr (requires { *opt; }) {
            return opt ? *opt : T{};
        } else {
            return opt;
        }
    } else if constexpr (std::is_same_v<T, types::Json>) {
        return types::Json(std::string(s));
    } else if constexpr (std::is_same_v<T, types::Blob>) {
        if (s.starts_with("\\x") || s.starts_with("0x")) {
            auto opt = types::Blob::from_hex(s);
            return opt ? *opt : types::Blob{};
        }
        auto opt = types::Blob::from_base64(s);
        return opt ? *opt : types::Blob{};
    } else if constexpr (std::is_same_v<T, types::Hash256>) {
        if (s.starts_with("\\x") || s.starts_with("0x")) {
            s.remove_prefix(2);
        }
        auto opt = types::Hash256::from_hex(s);
        return opt ? *opt : types::Hash256{};
    } else {
        return T{};
    }
}

// In-memory MockRowView for testing and driver adaptation
class MockRowView : public RowView {
    std::vector<std::optional<std::string>> values_;

public:
    MockRowView() = default;
    explicit MockRowView(std::vector<std::optional<std::string>> values)
        : values_(std::move(values)) {}

    void add_value(std::string val) {
        values_.push_back(std::move(val));
    }

    void add_null() {
        values_.push_back(std::nullopt);
    }

    [[nodiscard]] size_t column_count() const noexcept override {
        return values_.size();
    }

    [[nodiscard]] bool is_null(size_t col_idx) const override {
        if (col_idx >= values_.size()) return true;
        return !values_[col_idx].has_value();
    }

    [[nodiscard]] std::string_view get_raw(size_t col_idx) const override {
        if (col_idx >= values_.size() || !values_[col_idx].has_value()) {
            return "";
        }
        return values_[col_idx].value();
    }
};

class OffsetRowView : public RowView {
    const RowView& inner_;
    size_t offset_;

public:
    OffsetRowView(const RowView& inner, size_t offset) : inner_(inner), offset_(offset) {}

    [[nodiscard]] size_t column_count() const noexcept override {
        size_t total = inner_.column_count();
        return total > offset_ ? total - offset_ : 0;
    }

    [[nodiscard]] bool is_null(size_t col_idx) const override {
        return inner_.is_null(col_idx + offset_);
    }

    [[nodiscard]] std::string_view get_raw(size_t col_idx) const override {
        return inner_.get_raw(col_idx + offset_);
    }
};

} // namespace aegon::data::orm::sql
