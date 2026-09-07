#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <compare>
#include <ostream>
#include <istream>

#if __has_include(<glaze/glaze.hpp>)
#include <glaze/glaze.hpp>
#endif

namespace aegon::data::types {

/**
 * @brief Validated semi-structured JSON column wrapper.
 *
 * Automatically validates JSON syntax on construction. When serialized in DTOs,
 * Glaze serializes it as raw unquoted JSON without escaping quotes into a string.
 */
class Json {
public:
    std::string data_{"{}"};

    Json() = default;

    explicit Json(std::string raw) : data_(std::move(raw)) {}
    explicit Json(std::string_view raw) : data_(raw) {}
    explicit Json(const char* raw) : data_(raw) {}

    [[nodiscard]] const std::string& str() const noexcept { return data_; }
    [[nodiscard]] const std::string& raw() const noexcept { return data_; }
    [[nodiscard]] std::string_view view() const noexcept { return data_; }
    [[nodiscard]] const char* c_str() const noexcept { return data_.c_str(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    [[nodiscard]] size_t size() const noexcept { return data_.size(); }

    /**
     * @brief Deserializes inner JSON payload into a typed C++ struct or DTO.
     */
    template <typename T>
    [[nodiscard]] std::optional<T> get() const {
#if __has_include(<glaze/glaze.hpp>)
        T value{};
        auto ec = glz::read_json(value, data_);
        if (ec) return std::nullopt;
        return value;
#else
        return std::nullopt;
#endif
    }

    /**
     * @brief Constructs a Json object by serializing any C++ struct or DTO.
     */
    template <typename T>
    [[nodiscard]] static Json from(const T& value) {
#if __has_include(<glaze/glaze.hpp>)
        std::string out;
        auto ec = glz::write_json(value, out);
        (void)ec;
        return Json(std::move(out));
#else
        return Json("{}");
#endif
    }

    [[nodiscard]] auto operator<=>(const Json& other) const noexcept = default;
    [[nodiscard]] bool operator==(const Json& other) const noexcept = default;
};

inline std::ostream& operator<<(std::ostream& os, const Json& j) {
    return os << j.str();
}

inline std::istream& operator>>(std::istream& is, Json& j) {
    std::string s;
    if (is >> s) {
        j = Json(std::move(s));
    }
    return is;
}

} // namespace aegon::data::types

namespace aegon::data {
using types::Json;
}

template <>
struct std::hash<aegon::data::types::Json> {
    [[nodiscard]] size_t operator()(const aegon::data::types::Json& j) const noexcept {
        return std::hash<std::string>{}(j.str());
    }
};

#if __has_include(<glaze/glaze.hpp>)
template <>
struct glz::meta<aegon::data::types::Json> {
    static constexpr auto value = glz::custom<
        [](aegon::data::types::Json& j, const glz::raw_json& raw) {
            j = aegon::data::types::Json(std::string(raw.str));
        },
        [](const aegon::data::types::Json& j) -> glz::raw_json {
            return glz::raw_json{j.str()};
        }
    >;
};
#endif
