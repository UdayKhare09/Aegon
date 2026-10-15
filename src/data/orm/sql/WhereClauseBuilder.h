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

    /**
     * @brief Return the stored predicates in SQL compilation order.
     */
    [[nodiscard]] const std::vector<Condition>& conditions() const noexcept {
        return conditions_;
    }

    /**
     * @brief Return mutable predicates; edits directly affect subsequent SQL compilation.
     * The caller must supply enough values for each operator (two for BETWEEN).
     */
    [[nodiscard]] std::vector<Condition>& conditions() noexcept {
        return conditions_;
    }

    [[nodiscard]] bool has_conditions() const noexcept {
        return !conditions_.empty();
    }

    /**
     * @brief Remove all WHERE predicates and return this builder.
     */
    Derived& clear_conditions() noexcept {
        conditions_.clear();
        return derived();
    }

    // --- Standard Binary Predicates: where / and_where / or_where ---

    /**
     * @brief Append a single-value predicate on the field's mapped column with AND; return this
     * builder.
     * Use the dedicated helpers for IN, NOT IN, and BETWEEN.
     */
    template <typename FieldType, typename ValueType>
    Derived& where(FieldType Entity::* field, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::And, schema().resolve_column_name(field), op, {make_sql_param(val)}, nullptr});
        return derived();
    }

    /**
     * @brief Append a single-value predicate on the named column with AND; return this builder.
     * Use the dedicated helpers for IN, NOT IN, and BETWEEN.
     */
    template <typename ValueType>
    Derived& where(std::string col, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::And, std::move(col), op, {make_sql_param(val)}, nullptr});
        return derived();
    }

    /**
     * @brief Append a single-value predicate on the field's mapped column with AND; return this
     * builder.
     * Use the dedicated helpers for IN, NOT IN, and BETWEEN.
     */
    template <typename FieldType, typename ValueType>
    Derived& and_where(FieldType Entity::* field, Op op, const ValueType& val) {
        return where(field, op, val);
    }

    /**
     * @brief Append a single-value predicate on the named column with AND; return this builder.
     * Use the dedicated helpers for IN, NOT IN, and BETWEEN.
     */
    template <typename ValueType>
    Derived& and_where(std::string col, Op op, const ValueType& val) {
        return where(std::move(col), op, val);
    }

    /**
     * @brief Append a single-value predicate on the field's mapped column with OR; return this
     * builder.
     * Use the dedicated helpers for IN, NOT IN, and BETWEEN.
     */
    template <typename FieldType, typename ValueType>
    Derived& or_where(FieldType Entity::* field, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::Or, schema().resolve_column_name(field), op, {make_sql_param(val)}, nullptr});
        return derived();
    }

    /**
     * @brief Append a single-value predicate on the named column with OR; return this builder.
     * Use the dedicated helpers for IN, NOT IN, and BETWEEN.
     */
    template <typename ValueType>
    Derived& or_where(std::string col, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::Or, std::move(col), op, {make_sql_param(val)}, nullptr});
        return derived();
    }

    // --- IN / NOT IN ---

    /**
     * @brief Append an IN predicate on the field's mapped column with AND; return this builder.
     * An empty list compiles to 1 = 0 without parameters.
     */
    template <typename FieldType, typename Container>
    Derived& where_in(FieldType Entity::* field, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::And, schema().resolve_column_name(field), Op::In, std::move(formatted), nullptr});
        return derived();
    }

    /**
     * @brief Append an IN predicate on the named column with AND; return this builder.
     * An empty list compiles to 1 = 0 without parameters.
     */
    template <typename Container>
    Derived& where_in(std::string col, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::And, std::move(col), Op::In, std::move(formatted), nullptr});
        return derived();
    }

    /**
     * @brief Append an IN predicate on the field's mapped column with OR; return this builder.
     * An empty list compiles to 1 = 0 without parameters.
     */
    template <typename FieldType, typename Container>
    Derived& or_where_in(FieldType Entity::* field, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::Or, schema().resolve_column_name(field), Op::In, std::move(formatted), nullptr});
        return derived();
    }

    /**
     * @brief Append an IN predicate on the named column with OR; return this builder.
     * An empty list compiles to 1 = 0 without parameters.
     */
    template <typename Container>
    Derived& or_where_in(std::string col, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::Or, std::move(col), Op::In, std::move(formatted), nullptr});
        return derived();
    }

    /**
     * @brief Append a NOT IN predicate on the field's mapped column with AND; return this
     * builder.
     * An empty list compiles to 1 = 1 without parameters.
     */
    template <typename FieldType, typename Container>
    Derived& where_not_in(FieldType Entity::* field, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::And, schema().resolve_column_name(field), Op::NotIn, std::move(formatted), nullptr});
        return derived();
    }

    /**
     * @brief Append a NOT IN predicate on the named column with AND; return this builder.
     * An empty list compiles to 1 = 1 without parameters.
     */
    template <typename Container>
    Derived& where_not_in(std::string col, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::And, std::move(col), Op::NotIn, std::move(formatted), nullptr});
        return derived();
    }

    /**
     * @brief Append a NOT IN predicate on the field's mapped column with OR; return this
     * builder.
     * An empty list compiles to 1 = 1 without parameters.
     */
    template <typename FieldType, typename Container>
    Derived& or_where_not_in(FieldType Entity::* field, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::Or, schema().resolve_column_name(field), Op::NotIn, std::move(formatted), nullptr});
        return derived();
    }

    /**
     * @brief Append a NOT IN predicate on the named column with OR; return this builder.
     * An empty list compiles to 1 = 1 without parameters.
     */
    template <typename Container>
    Derived& or_where_not_in(std::string col, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) formatted.push_back(make_sql_param(item));
        conditions_.push_back({Conjunction::Or, std::move(col), Op::NotIn, std::move(formatted), nullptr});
        return derived();
    }

    // --- NULL / NOT NULL ---

    /**
     * @brief Append an IS NULL predicate on the field's mapped column with AND; return this
     * builder.
     */
    template <typename FieldType>
    Derived& where_null(FieldType Entity::* field) {
        conditions_.push_back({Conjunction::And, schema().resolve_column_name(field), Op::IsNull, {}, nullptr});
        return derived();
    }

    /**
     * @brief Append an IS NULL predicate on the named column with AND; return this builder.
     */
    Derived& where_null(std::string col) {
        conditions_.push_back({Conjunction::And, std::move(col), Op::IsNull, {}, nullptr});
        return derived();
    }

    /**
     * @brief Append an IS NULL predicate on the field's mapped column with OR; return this
     * builder.
     */
    template <typename FieldType>
    Derived& or_where_null(FieldType Entity::* field) {
        conditions_.push_back({Conjunction::Or, schema().resolve_column_name(field), Op::IsNull, {}, nullptr});
        return derived();
    }

    /**
     * @brief Append an IS NULL predicate on the named column with OR; return this builder.
     */
    Derived& or_where_null(std::string col) {
        conditions_.push_back({Conjunction::Or, std::move(col), Op::IsNull, {}, nullptr});
        return derived();
    }

    /**
     * @brief Append an IS NOT NULL predicate on the field's mapped column with AND; return this
     * builder.
     */
    template <typename FieldType>
    Derived& where_not_null(FieldType Entity::* field) {
        conditions_.push_back({Conjunction::And, schema().resolve_column_name(field), Op::IsNotNull, {}, nullptr});
        return derived();
    }

    /**
     * @brief Append an IS NOT NULL predicate on the named column with AND; return this builder.
     */
    Derived& where_not_null(std::string col) {
        conditions_.push_back({Conjunction::And, std::move(col), Op::IsNotNull, {}, nullptr});
        return derived();
    }

    /**
     * @brief Append an IS NOT NULL predicate on the field's mapped column with OR; return this
     * builder.
     */
    template <typename FieldType>
    Derived& or_where_not_null(FieldType Entity::* field) {
        conditions_.push_back({Conjunction::Or, schema().resolve_column_name(field), Op::IsNotNull, {}, nullptr});
        return derived();
    }

    /**
     * @brief Append an IS NOT NULL predicate on the named column with OR; return this builder.
     */
    Derived& or_where_not_null(std::string col) {
        conditions_.push_back({Conjunction::Or, std::move(col), Op::IsNotNull, {}, nullptr});
        return derived();
    }

    // --- BETWEEN ---

    /**
     * @brief Append an inclusive BETWEEN predicate on the field's mapped column with AND;
     * return this builder.
     * The low and high bounds are bound in that order without validation.
     */
    template <typename FieldType, typename LowType, typename HighType>
    Derived& where_between(FieldType Entity::* field, const LowType& low, const HighType& high) {
        conditions_.push_back({Conjunction::And, schema().resolve_column_name(field), Op::Between, {make_sql_param(low), make_sql_param(high)}, nullptr});
        return derived();
    }

    /**
     * @brief Append an inclusive BETWEEN predicate on the named column with AND; return this
     * builder.
     * The low and high bounds are bound in that order without validation.
     */
    template <typename LowType, typename HighType>
    Derived& where_between(std::string col, const LowType& low, const HighType& high) {
        conditions_.push_back({Conjunction::And, std::move(col), Op::Between, {make_sql_param(low), make_sql_param(high)}, nullptr});
        return derived();
    }

    /**
     * @brief Append an inclusive BETWEEN predicate on the field's mapped column with OR; return
     * this builder.
     * The low and high bounds are bound in that order without validation.
     */
    template <typename FieldType, typename LowType, typename HighType>
    Derived& or_where_between(FieldType Entity::* field, const LowType& low, const HighType& high) {
        conditions_.push_back({Conjunction::Or, schema().resolve_column_name(field), Op::Between, {make_sql_param(low), make_sql_param(high)}, nullptr});
        return derived();
    }

    /**
     * @brief Append an inclusive BETWEEN predicate on the named column with OR; return this
     * builder.
     * The low and high bounds are bound in that order without validation.
     */
    template <typename LowType, typename HighType>
    Derived& or_where_between(std::string col, const LowType& low, const HighType& high) {
        conditions_.push_back({Conjunction::Or, std::move(col), Op::Between, {make_sql_param(low), make_sql_param(high)}, nullptr});
        return derived();
    }

    // --- LIKE / NOT LIKE ---

    /**
     * @brief Append a LIKE predicate on the field's mapped column with AND; return this
     * builder.
     * The pattern is bound as supplied; wildcards are not escaped.
     */
    template <typename FieldType, typename ValueType>
    Derived& where_like(FieldType Entity::* field, const ValueType& val) {
        return where(field, Op::Like, val);
    }

    /**
     * @brief Append a LIKE predicate on the named column with AND; return this builder.
     * The pattern is bound as supplied; wildcards are not escaped.
     */
    template <typename ValueType>
    Derived& where_like(std::string col, const ValueType& val) {
        return where(std::move(col), Op::Like, val);
    }

    /**
     * @brief Append a LIKE predicate on the field's mapped column with OR; return this builder.
     * The pattern is bound as supplied; wildcards are not escaped.
     */
    template <typename FieldType, typename ValueType>
    Derived& or_where_like(FieldType Entity::* field, const ValueType& val) {
        return or_where(field, Op::Like, val);
    }

    /**
     * @brief Append a LIKE predicate on the named column with OR; return this builder.
     * The pattern is bound as supplied; wildcards are not escaped.
     */
    template <typename ValueType>
    Derived& or_where_like(std::string col, const ValueType& val) {
        return or_where(std::move(col), Op::Like, val);
    }

    /**
     * @brief Append a NOT LIKE predicate on the field's mapped column with AND; return this
     * builder.
     * The pattern is bound as supplied; wildcards are not escaped.
     */
    template <typename FieldType, typename ValueType>
    Derived& where_not_like(FieldType Entity::* field, const ValueType& val) {
        return where(field, Op::NotLike, val);
    }

    /**
     * @brief Append a NOT LIKE predicate on the named column with AND; return this builder.
     * The pattern is bound as supplied; wildcards are not escaped.
     */
    template <typename ValueType>
    Derived& where_not_like(std::string col, const ValueType& val) {
        return where(std::move(col), Op::NotLike, val);
    }

    /**
     * @brief Append a NOT LIKE predicate on the field's mapped column with OR; return this
     * builder.
     * The pattern is bound as supplied; wildcards are not escaped.
     */
    template <typename FieldType, typename ValueType>
    Derived& or_where_not_like(FieldType Entity::* field, const ValueType& val) {
        return or_where(field, Op::NotLike, val);
    }

    /**
     * @brief Append a NOT LIKE predicate on the named column with OR; return this builder.
     * The pattern is bound as supplied; wildcards are not escaped.
     */
    template <typename ValueType>
    Derived& or_where_not_like(std::string col, const ValueType& val) {
        return or_where(std::move(col), Op::NotLike, val);
    }
};

} // namespace aegon::data::orm::sql
