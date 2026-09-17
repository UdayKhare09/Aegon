#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <optional>
#include <cstdint>
#include <charconv>

namespace aegon::data::redis {

enum class RespType : uint8_t {
    Null,
    SimpleString,
    Error,
    Integer,
    BulkString,
    Array,
    Boolean,
    Double,
    Push
};

struct RespValue;

using RespArray = std::vector<RespValue>;

struct RespValue {
    RespType type{RespType::Null};
    std::variant<
        std::monostate,           // Null
        std::string,              // SimpleString, Error, BulkString
        int64_t,                  // Integer
        double,                   // Double
        bool,                     // Boolean
        RespArray                 // Array, Push
    > data;

    [[nodiscard]] bool is_null() const noexcept { return type == RespType::Null; }
    [[nodiscard]] bool is_error() const noexcept { return type == RespType::Error; }
    [[nodiscard]] bool is_string() const noexcept { 
        return type == RespType::SimpleString || type == RespType::BulkString; 
    }
    [[nodiscard]] bool is_integer() const noexcept { return type == RespType::Integer; }
    [[nodiscard]] bool is_boolean() const noexcept { return type == RespType::Boolean; }
    [[nodiscard]] bool is_double() const noexcept { return type == RespType::Double; }
    [[nodiscard]] bool is_array() const noexcept { return type == RespType::Array || type == RespType::Push; }

    [[nodiscard]] std::string_view as_string() const {
        if (auto* s = std::get_if<std::string>(&data)) {
            return *s;
        }
        return {};
    }

    [[nodiscard]] int64_t as_integer() const {
        if (auto* i = std::get_if<int64_t>(&data)) {
            return *i;
        }
        return 0;
    }

    [[nodiscard]] bool as_boolean() const {
        if (auto* b = std::get_if<bool>(&data)) {
            return *b;
        }
        return false;
    }

    [[nodiscard]] double as_double() const {
        if (auto* d = std::get_if<double>(&data)) {
            return *d;
        }
        return 0.0;
    }

    [[nodiscard]] const RespArray& as_array() const {
        static const RespArray empty_array;
        if (auto* a = std::get_if<RespArray>(&data)) {
            return *a;
        }
        return empty_array;
    }
};

enum class ParseStatus {
    Done,
    NeedMoreData,
    Error
};

class Resp3Parser {
public:
    static ParseStatus parse(std::string_view& in, RespValue& out);
};

class Resp3Serializer {
public:
    static std::string serialize_command(const std::vector<std::string_view>& args);
};

} // namespace aegon::data::redis
