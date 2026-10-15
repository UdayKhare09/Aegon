#pragma once

#include "Dialect.h"
#include "Table.h"
#include "Expression.h"
#include "SqlParam.h"
#include <string>
#include <vector>
#include <span>
#include <utility>

namespace aegon::data::orm::sql {

/**
 * @brief Reusable CRTP mixin providing complete WHERE condition construction
 *        for SelectBuilder, UpdateBuilder, and DeleteBuilder.
 */
template <typename Derived, typename Entity>
class WhereClauseBuilder {
protected:
    std::vector<Condition> conditions_;

    [[nodiscard]] Derived& derived() noexcept {
        return static_cast<Derived&>(*this);
    }

    [[nodiscard]] const Derived& derived() const noexcept {
        return static_cast<const Derived&>(*this);
    }

    [[nodiscard]] const TableDef<Entity>& schema() const noexcept {
        return derived().schema();
    }

public:
    WhereClauseBuilder() = default;
    WhereClauseBuilder(const WhereClauseBuilder&) = default;
    WhereClauseBuilder(WhereClauseBuilder&&) noexcept = default;
    WhereClauseBuilder& operator=(const WhereClauseBuilder&) = default;
    WhereClauseBuilder& operator=(WhereClauseBuilder&&) noexcept = default;
    ~WhereClauseBuilder() = default;

    // --- Inspection & Manipulation ---

    [[nodiscard]] const std::vector<Condition>& conditions() const noexcept {
        return conditions_;
    }

    [[nodiscard]] std::vector<Condition>& conditions() noexcept {
        return conditions_;
    }

    [[nodiscard]] bool has_conditions() const noexcept {
        return !conditions_.empty();
    }

    Derived& clear_conditions() noexcept {
        conditions_.clear();
        return derived();
    }

    // --- Standard Binary Predicates: where / and_where / or_where ---

    template <typename FieldType, typename ValueType>
    Derived& where(FieldType Entity::* field, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::And, schema().resolve_column_name(field), op, {make_sql_param(val)}, nullptr});
        return derived();
    }

    template <typename ValueType>
    Derived& where(std::string col, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::And, std::move(col), op, {make_sql_param(val)}, nullptr});
        return derived();
    }

    template <typename FieldType, typename ValueType>
    Derived& and_where(FieldType Entity::* field, Op op, const ValueType& val) {
        return where(field, op, val);
    }

    template <typename ValueType>
    Derived& and_where(std::string col, Op op, const ValueType& val) {
        return where(std::move(col), op, val);
    }

    template <typename FieldType, typename ValueType>
    Derived& or_where(FieldType Entity::* field, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::Or, schema().resolve_column_name(field), op, {make_sql_param(val)}, nullptr});
        return derived();
    }

    template <typename ValueType>
    Derived& or_where(std::string col, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::Or, std::move(col), op, {make_sql_param(val)}, nullptr});
        return derived();
    }

    // --- IN / NOT IN ---

    template <typename FieldType, typename Container>
    Derived& where_in(FieldType Entity::* field, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::And, schema().resolve_column_name(field), Op::In, std::move(formatted), nullptr});
        return derived();
    }

    template <typename Container>
    Derived& where_in(std::string col, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::And, std::move(col), Op::In, std::move(formatted), nullptr});
        return derived();
    }

    template <typename FieldType, typename Container>
    Derived& or_where_in(FieldType Entity::* field, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::Or, schema().resolve_column_name(field), Op::In, std::move(formatted), nullptr});
        return derived();
    }

    template <typename Container>
    Derived& or_where_in(std::string col, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::Or, std::move(col), Op::In, std::move(formatted), nullptr});
        return derived();
    }

    template <typename FieldType, typename Container>
    Derived& where_not_in(FieldType Entity::* field, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::And, schema().resolve_column_name(field), Op::NotIn, std::move(formatted), nullptr});
        return derived();
    }

    template <typename Container>
    Derived& where_not_in(std::string col, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::And, std::move(col), Op::NotIn, std::move(formatted), nullptr});
        return derived();
    }

    template <typename FieldType, typename Container>
    Derived& or_where_not_in(FieldType Entity::* field, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::Or, schema().resolve_column_name(field), Op::NotIn, std::move(formatted), nullptr});
        return derived();
    }

    template <typename Container>
    Derived& or_where_not_in(std::string col, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::Or, std::move(col), Op::NotIn, std::move(formatted), nullptr});
        return derived();
    }

    // --- NULL / NOT NULL ---

    template <typename FieldType>
    Derived& where_null(FieldType Entity::* field) {
        conditions_.push_back({Conjunction::And, schema().resolve_column_name(field), Op::IsNull, {}, nullptr});
        return derived();
    }

    Derived& where_null(std::string col) {
        conditions_.push_back({Conjunction::And, std::move(col), Op::IsNull, {}, nullptr});
        return derived();
    }

    template <typename FieldType>
    Derived& or_where_null(FieldType Entity::* field) {
        conditions_.push_back({Conjunction::Or, schema().resolve_column_name(field), Op::IsNull, {}, nullptr});
        return derived();
    }

    Derived& or_where_null(std::string col) {
        conditions_.push_back({Conjunction::Or, std::move(col), Op::IsNull, {}, nullptr});
        return derived();
    }

    template <typename FieldType>
    Derived& where_not_null(FieldType Entity::* field) {
        conditions_.push_back({Conjunction::And, schema().resolve_column_name(field), Op::IsNotNull, {}, nullptr});
        return derived();
    }

    Derived& where_not_null(std::string col) {
        conditions_.push_back({Conjunction::And, std::move(col), Op::IsNotNull, {}, nullptr});
        return derived();
    }

    template <typename FieldType>
    Derived& or_where_not_null(FieldType Entity::* field) {
        conditions_.push_back({Conjunction::Or, schema().resolve_column_name(field), Op::IsNotNull, {}, nullptr});
        return derived();
    }

    Derived& or_where_not_null(std::string col) {
        conditions_.push_back({Conjunction::Or, std::move(col), Op::IsNotNull, {}, nullptr});
        return derived();
    }

    // --- BETWEEN ---

    template <typename FieldType, typename LowType, typename HighType>
    Derived& where_between(FieldType Entity::* field, const LowType& low, const HighType& high) {
        conditions_.push_back({Conjunction::And, schema().resolve_column_name(field), Op::Between, {make_sql_param(low), make_sql_param(high)}, nullptr});
        return derived();
    }

    template <typename LowType, typename HighType>
    Derived& where_between(std::string col, const LowType& low, const HighType& high) {
        conditions_.push_back({Conjunction::And, std::move(col), Op::Between, {make_sql_param(low), make_sql_param(high)}, nullptr});
        return derived();
    }

    template <typename FieldType, typename LowType, typename HighType>
    Derived& or_where_between(FieldType Entity::* field, const LowType& low, const HighType& high) {
        conditions_.push_back({Conjunction::Or, schema().resolve_column_name(field), Op::Between, {make_sql_param(low), make_sql_param(high)}, nullptr});
        return derived();
    }

    template <typename LowType, typename HighType>
    Derived& or_where_between(std::string col, const LowType& low, const HighType& high) {
        conditions_.push_back({Conjunction::Or, std::move(col), Op::Between, {make_sql_param(low), make_sql_param(high)}, nullptr});
        return derived();
    }

    // --- LIKE / NOT LIKE ---

    template <typename FieldType, typename ValueType>
    Derived& where_like(FieldType Entity::* field, const ValueType& val) {
        return where(field, Op::Like, val);
    }

    template <typename ValueType>
    Derived& where_like(std::string col, const ValueType& val) {
        return where(std::move(col), Op::Like, val);
    }

    template <typename FieldType, typename ValueType>
    Derived& or_where_like(FieldType Entity::* field, const ValueType& val) {
        return or_where(field, Op::Like, val);
    }

    template <typename ValueType>
    Derived& or_where_like(std::string col, const ValueType& val) {
        return or_where(std::move(col), Op::Like, val);
    }

    template <typename FieldType, typename ValueType>
    Derived& where_not_like(FieldType Entity::* field, const ValueType& val) {
        return where(field, Op::NotLike, val);
    }

    template <typename ValueType>
    Derived& where_not_like(std::string col, const ValueType& val) {
        return where(std::move(col), Op::NotLike, val);
    }

    template <typename FieldType, typename ValueType>
    Derived& or_where_not_like(FieldType Entity::* field, const ValueType& val) {
        return or_where(field, Op::NotLike, val);
    }

    template <typename ValueType>
    Derived& or_where_not_like(std::string col, const ValueType& val) {
        return or_where(std::move(col), Op::NotLike, val);
    }
};

} // namespace aegon::data::orm::sql
