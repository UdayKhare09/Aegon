#pragma once

#include "Dialect.h"
#include "data/types/Types.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <type_traits>

namespace aegon::data::orm::sql {

enum class Op : uint8_t {
    Eq,
    Neq,
    Gt,
    Gte,
    Lt,
    Lte,
    Like,
    NotLike,
    In,
    NotIn,
    IsNull,
    IsNotNull,
    Between
};

enum class SortOrder : uint8_t {
    Asc,
    Desc
};

enum class Conjunction : uint8_t {
    And,
    Or
};

using OrderByDirection = SortOrder;

constexpr std::string_view op_to_sql(Op op) noexcept {
    switch (op) {
        case Op::Eq:        return "=";
        case Op::Neq:       return "!=";
        case Op::Gt:        return ">";
        case Op::Gte:       return ">=";
        case Op::Lt:        return "<";
        case Op::Lte:       return "<=";
        case Op::Like:      return "LIKE";
        case Op::NotLike:   return "NOT LIKE";
        case Op::In:        return "IN";
        case Op::NotIn:     return "NOT IN";
        case Op::IsNull:    return "IS NULL";
        case Op::IsNotNull: return "IS NOT NULL";
        case Op::Between:   return "BETWEEN";
    }
    return "=";
}

constexpr std::string_view order_to_sql(SortOrder ord) noexcept {
    switch (ord) {
        case SortOrder::Asc:  return "ASC";
        case SortOrder::Desc: return "DESC";
    }
    return "ASC";
}

using namespace std::string_view_literals;
inline constexpr std::string_view SQL_NULL_SENTINEL = "\0__AEGON_NULL__\0"sv;

template <typename T>
inline std::string format_param_value(const T& val) {
    using Decayed = std::decay_t<T>;
    if constexpr (requires { val.has_value(); }) {
        if (val.has_value()) {
            return format_param_value(*val);
        }
        return std::string(SQL_NULL_SENTINEL);
    } else if constexpr (std::is_convertible_v<T, std::string_view>) {
        return std::string(std::string_view(val));
    } else if constexpr (std::is_same_v<Decayed, bool>) {
        return val ? "true" : "false";
    } else if constexpr (std::is_arithmetic_v<Decayed>) {
        return std::to_string(val);
    } else if constexpr (std::is_same_v<Decayed, types::UUID>) {
        return val.to_string();
    } else if constexpr (std::is_same_v<Decayed, types::DateTime>) {
        return val.to_iso8601();
    } else if constexpr (std::is_same_v<Decayed, types::Date>) {
        return val.to_string();
    } else if constexpr (std::is_same_v<Decayed, types::Time>) {
        return val.to_string();
    } else if constexpr (std::is_same_v<Decayed, types::Json>) {
        return val.str();
    } else if constexpr (std::is_same_v<Decayed, types::Blob>) {
        return "\\x" + val.to_hex();
    } else if constexpr (std::is_same_v<Decayed, types::Hash256>) {
        return "\\x" + val.to_hex();
    } else if constexpr (requires { val.to_string(); }) {
        return val.to_string();
    } else {
        return "";
    }
}

struct Condition {
    Conjunction conj{Conjunction::And};
    std::string column;
    Op op{Op::Eq};
    std::vector<std::string> values;
};

struct OrderByClause {
    std::string column;
    SortOrder direction{SortOrder::Asc};
};

} // namespace aegon::data::orm::sql
