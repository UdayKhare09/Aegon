#pragma once

#include "Dialect.h"
#include "SqlParam.h"
#include <string>
#include <string_view>
#include <vector>
#include <span>
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

// ─── JOIN support ────────────────────────────────────────────────────────────

enum class JoinType : uint8_t {
    Inner,
    Left,
    Right,
    Full
};

constexpr std::string_view join_type_to_sql(JoinType jt) noexcept {
    switch (jt) {
        case JoinType::Inner: return "INNER";
        case JoinType::Left:  return "LEFT";
        case JoinType::Right: return "RIGHT";
        case JoinType::Full:  return "FULL OUTER";
    }
    return "INNER";
}

struct JoinClause {
    JoinType    type{JoinType::Inner};
    std::string table;     // joined table name (unquoted)
    std::string on_expr;   // raw ON expression, e.g. "\"users\".\"id\" = \"orders\".\"user_id\""
};

// ─── Condition ───────────────────────────────────────────────────────────────

struct Condition {
    Conjunction conj{Conjunction::And};
    std::string column;
    Op op{Op::Eq};
    std::vector<SqlParam> values;
    // Custom compiler for sub-select / EXISTS / raw expressions.
    // Takes (dialect, param_idx, out_params) and returns an SQL fragment.
    std::function<std::string(DatabaseDialect, size_t&, std::vector<SqlParam>&)> custom_compiler;
};

struct OrderByClause {
    std::string column;
    SortOrder direction{SortOrder::Asc};
};

// ─── Condition compilation ───────────────────────────────────────────────────

void compile_condition(const Condition& cond, DatabaseDialect dialect,
                       size_t& param_idx, std::string& sql,
                       std::vector<SqlParam>& out_params);

void compile_conditions(std::span<const Condition> conditions, DatabaseDialect dialect,
                        size_t& param_idx, std::string& sql,
                        std::vector<SqlParam>& out_params);

} // namespace aegon::data::orm::sql
