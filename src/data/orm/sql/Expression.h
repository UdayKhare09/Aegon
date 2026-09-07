#pragma once

#include "Dialect.h"
#include "data/types/Types.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <type_traits>
#include <functional>

namespace aegon::data::orm::sql {

template <typename T>
struct unwrapped_type {
    using type = T;
};

template <typename T>
struct unwrapped_type<std::optional<T>> {
    using type = T;
};

template <typename T>
using unwrapped_type_t = typename unwrapped_type<T>::type;

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
    std::function<std::string(DatabaseDialect, size_t&, std::vector<std::string>&)> custom_compiler;
};

struct OrderByClause {
    std::string column;
    SortOrder direction{SortOrder::Asc};
};

inline void compile_condition(const Condition& cond, DatabaseDialect dialect, size_t& param_idx, std::string& sql, std::vector<std::string>& out_params) {
    if (cond.custom_compiler) {
        sql.append(cond.custom_compiler(dialect, param_idx, out_params));
        return;
    }

    sql.append(DialectTraits::quote_identifier(dialect, cond.column));
    sql.push_back(' ');

    if (cond.op == Op::IsNull || cond.op == Op::IsNotNull) {
        sql.append(op_to_sql(cond.op));
    } else if (cond.op == Op::Between) {
        sql.append("BETWEEN ");
        std::string p1, p2;
        DialectTraits::format_placeholder(dialect, param_idx++, p1);
        DialectTraits::format_placeholder(dialect, param_idx++, p2);
        sql.append(p1).append(" AND ").append(p2);
        out_params.push_back(cond.values[0]);
        out_params.push_back(cond.values[1]);
    } else if (cond.op == Op::In || cond.op == Op::NotIn) {
        sql.append(op_to_sql(cond.op));
        sql.append(" (");
        for (size_t j = 0; j < cond.values.size(); ++j) {
            std::string p;
            DialectTraits::format_placeholder(dialect, param_idx++, p);
            sql.append(p);
            if (j + 1 < cond.values.size()) sql.append(", ");
            out_params.push_back(cond.values[j]);
        }
        sql.push_back(')');
    } else {
        sql.append(op_to_sql(cond.op));
        sql.push_back(' ');
        std::string p;
        DialectTraits::format_placeholder(dialect, param_idx++, p);
        sql.append(p);
        out_params.push_back(cond.values[0]);
    }
}

inline void compile_conditions(const std::vector<Condition>& conditions, DatabaseDialect dialect, size_t& param_idx, std::string& sql, std::vector<std::string>& out_params) {
    for (size_t i = 0; i < conditions.size(); ++i) {
        const auto& cond = conditions[i];
        if (i > 0) {
            sql.append(cond.conj == Conjunction::Or ? " OR " : " AND ");
        }
        compile_condition(cond, dialect, param_idx, sql, out_params);
    }
}

} // namespace aegon::data::orm::sql
