#pragma once

#include "Dialect.h"
#include "Table.h"
#include "Expression.h"
#include "QueryResult.h"
#include "RowView.h"
#include <string>
#include <vector>
#include <optional>
#include <concepts>
#include <span>
#include <functional>

namespace aegon::data::orm::sql {

template <typename T>
struct Page {
    std::vector<T> items{};
    size_t total_items{0};
    size_t current_page{1};
    size_t per_page{20};
    size_t total_pages{0};
    bool has_next{false};
    bool has_prev{false};
};

template <typename Entity>
class SelectBuilder {
    TableDef<Entity> schema_;
    std::vector<std::string> projected_columns_;
    std::vector<Condition> conditions_;
    std::vector<OrderByClause> order_bys_;
    std::vector<std::string> group_bys_;
    std::vector<Condition> havings_;
    std::optional<size_t> limit_;
    std::optional<size_t> offset_;
    std::vector<std::function<core::Task<void>(std::span<Entity>, Connection&, DatabaseDialect)>> includes_;
    bool is_cached_{false};
    std::optional<std::chrono::seconds> cache_ttl_{std::nullopt};
    std::optional<InvalidationMode> cache_invalidation_override_{std::nullopt};
    std::optional<std::string> partition_value_{std::nullopt};

public:
    SelectBuilder() : schema_(Entity::schema()) {}
    explicit SelectBuilder(TableDef<Entity> schema) : schema_(std::move(schema)) {}

    template <typename Val>
    SelectBuilder& partition(const Val& v) {
        partition_value_ = format_param_value(v);
        return *this;
    }

    [[nodiscard]] std::optional<std::string> resolve_partition_value() const {
        if (partition_value_) return partition_value_;
        if (schema_.cache_config().partition_column) {
            const auto& target_col = *schema_.cache_config().partition_column;
            for (const auto& cond : conditions_) {
                if (cond.conj == Conjunction::And && cond.column == target_col && cond.op == Op::Eq && !cond.values.empty()) {
                    return cond.values[0];
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] const TableDef<Entity>& schema() const noexcept { return schema_; }
    [[nodiscard]] const std::vector<Condition>& conditions() const noexcept { return conditions_; }

    [[nodiscard]] std::vector<std::pair<std::string, std::string>> extract_equality_predicates() const {
        std::vector<std::pair<std::string, std::string>> preds;
        for (const auto& cond : conditions_) {
            if (cond.conj == Conjunction::And && cond.op == Op::Eq && !cond.values.empty()) {
                preds.emplace_back(cond.column, cond.values[0]);
            }
        }
        return preds;
    }

    [[nodiscard]] const std::vector<OrderByClause>& order_bys() const noexcept { return order_bys_; }
    [[nodiscard]] const std::vector<std::string>& group_bys() const noexcept { return group_bys_; }
    [[nodiscard]] const std::vector<Condition>& havings() const noexcept { return havings_; }
    [[nodiscard]] const std::optional<size_t>& limit_value() const noexcept { return limit_; }
    [[nodiscard]] const std::optional<size_t>& offset_value() const noexcept { return offset_; }

    SelectBuilder& clear_limit() noexcept {
        limit_.reset();
        return *this;
    }

    SelectBuilder& clear_offset() noexcept {
        offset_.reset();
        return *this;
    }

    SelectBuilder& clear_order_by() noexcept {
        order_bys_.clear();
        return *this;
    }

    // --- Caching ---
    SelectBuilder& cached(std::optional<std::chrono::seconds> ttl = std::nullopt) {
        is_cached_ = true;
        cache_ttl_ = ttl;
        return *this;
    }

    SelectBuilder& cached(InvalidationMode mode, std::optional<std::chrono::seconds> ttl = std::nullopt) {
        is_cached_ = true;
        cache_invalidation_override_ = mode;
        cache_ttl_ = ttl;
        return *this;
    }

    [[nodiscard]] bool is_cached() const noexcept {
        return is_cached_ || schema_.is_cached();
    }

    [[nodiscard]] std::optional<std::chrono::seconds> cache_ttl() const noexcept {
        if (cache_ttl_) return cache_ttl_;
        if (schema_.is_cached()) return schema_.cache_config().ttl;
        return std::nullopt;
    }

    [[nodiscard]] InvalidationMode invalidation_mode() const noexcept {
        if (cache_invalidation_override_) return *cache_invalidation_override_;
        return schema_.cache_config().invalidation;
    }

    [[nodiscard]] std::string compute_query_fingerprint(const QueryResult& q) const {
        uint64_t h = 14695981039346656037ULL;
        for (char c : q.sql) {
            h ^= static_cast<uint8_t>(c);
            h *= 1099511628211ULL;
        }
        for (const auto& p : q.params) {
            h ^= 0x5c;
            h *= 1099511628211ULL;
            for (char c : p) {
                h ^= static_cast<uint8_t>(c);
                h *= 1099511628211ULL;
            }
        }
        return std::to_string(h);
    }

    // Includes (Unscoped)
    template <typename TargetField>
    SelectBuilder& include(TargetField Entity::* rel_ptr) {
        const auto* desc = schema_.find_relation(rel_ptr);
        if (desc && desc->eager_loader) {
            includes_.push_back(desc->eager_loader);
        }
        return *this;
    }

    // Scoped Include for HasMany (1:N or N:M)
    template <typename RelEntity, typename Func>
    SelectBuilder& include(HasMany<RelEntity> Entity::* rel_ptr, Func&& filter) {
        const auto* desc = schema_.find_relation(rel_ptr);
        if (desc && desc->scoped_eager_loader) {
            SelectBuilder<RelEntity> child_builder;
            filter(child_builder);
            auto child_conds = child_builder.conditions();
            auto child_orders = child_builder.order_bys();
            auto child_lim = child_builder.limit_value();
            auto scoped_fn = desc->scoped_eager_loader;
            includes_.push_back([scoped_fn, child_conds = std::move(child_conds), child_orders = std::move(child_orders), child_lim](
                std::span<Entity> parents, Connection& conn, DatabaseDialect dialect) -> core::Task<void> {
                co_await scoped_fn(parents, conn, dialect, child_conds, child_orders, child_lim);
            });
        }
        return *this;
    }

    // Scoped Include for HasOne (1:1)
    template <typename RelEntity, typename Func>
    SelectBuilder& include(HasOne<RelEntity> Entity::* rel_ptr, Func&& filter) {
        const auto* desc = schema_.find_relation(rel_ptr);
        if (desc && desc->scoped_eager_loader) {
            SelectBuilder<RelEntity> child_builder;
            filter(child_builder);
            auto child_conds = child_builder.conditions();
            auto child_orders = child_builder.order_bys();
            auto child_lim = child_builder.limit_value();
            auto scoped_fn = desc->scoped_eager_loader;
            includes_.push_back([scoped_fn, child_conds = std::move(child_conds), child_orders = std::move(child_orders), child_lim](
                std::span<Entity> parents, Connection& conn, DatabaseDialect dialect) -> core::Task<void> {
                co_await scoped_fn(parents, conn, dialect, child_conds, child_orders, child_lim);
            });
        }
        return *this;
    }

    [[nodiscard]] bool has_includes() const noexcept {
        return !includes_.empty();
    }

    core::Task<void> eager_load_includes(std::span<Entity> entities, Connection& conn, DatabaseDialect dialect) const {
        for (const auto& loader : includes_) {
            co_await loader(entities, conn, dialect);
        }
    }

    // Relation filtering: where_has & where_doesnt_have
    template <typename RelEntity, typename Func>
    SelectBuilder& where_has(HasMany<RelEntity> Entity::* rel_ptr, Func&& filter) {
        const auto* desc = schema_.find_relation(rel_ptr);
        if (desc && desc->exists_builder) {
            SelectBuilder<RelEntity> child_builder;
            filter(child_builder);
            auto child_conds = child_builder.conditions();
            auto exists_fn = desc->exists_builder;
            std::string parent_tbl = schema_.table_name();

            Condition c;
            c.conj = Conjunction::And;
            c.custom_compiler = [exists_fn, parent_tbl, child_conds = std::move(child_conds)](
                DatabaseDialect dialect, size_t& param_idx, std::vector<std::string>& out_params) -> std::string {
                return exists_fn(dialect, false, parent_tbl, child_conds, param_idx, out_params);
            };
            conditions_.push_back(std::move(c));
        }
        return *this;
    }

    template <typename RelEntity>
    SelectBuilder& where_has(HasMany<RelEntity> Entity::* rel_ptr) {
        return where_has(rel_ptr, [](auto&) {});
    }

    template <typename RelEntity, typename Func>
    SelectBuilder& where_has(HasOne<RelEntity> Entity::* rel_ptr, Func&& filter) {
        const auto* desc = schema_.find_relation(rel_ptr);
        if (desc && desc->exists_builder) {
            SelectBuilder<RelEntity> child_builder;
            filter(child_builder);
            auto child_conds = child_builder.conditions();
            auto exists_fn = desc->exists_builder;
            std::string parent_tbl = schema_.table_name();

            Condition c;
            c.conj = Conjunction::And;
            c.custom_compiler = [exists_fn, parent_tbl, child_conds = std::move(child_conds)](
                DatabaseDialect dialect, size_t& param_idx, std::vector<std::string>& out_params) -> std::string {
                return exists_fn(dialect, false, parent_tbl, child_conds, param_idx, out_params);
            };
            conditions_.push_back(std::move(c));
        }
        return *this;
    }

    template <typename RelEntity>
    SelectBuilder& where_has(HasOne<RelEntity> Entity::* rel_ptr) {
        return where_has(rel_ptr, [](auto&) {});
    }

    template <typename RelEntity, typename Func>
    SelectBuilder& where_doesnt_have(HasMany<RelEntity> Entity::* rel_ptr, Func&& filter) {
        const auto* desc = schema_.find_relation(rel_ptr);
        if (desc && desc->exists_builder) {
            SelectBuilder<RelEntity> child_builder;
            filter(child_builder);
            auto child_conds = child_builder.conditions();
            auto exists_fn = desc->exists_builder;
            std::string parent_tbl = schema_.table_name();

            Condition c;
            c.conj = Conjunction::And;
            c.custom_compiler = [exists_fn, parent_tbl, child_conds = std::move(child_conds)](
                DatabaseDialect dialect, size_t& param_idx, std::vector<std::string>& out_params) -> std::string {
                return exists_fn(dialect, true, parent_tbl, child_conds, param_idx, out_params);
            };
            conditions_.push_back(std::move(c));
        }
        return *this;
    }

    template <typename RelEntity>
    SelectBuilder& where_doesnt_have(HasMany<RelEntity> Entity::* rel_ptr) {
        return where_doesnt_have(rel_ptr, [](auto&) {});
    }

    template <typename RelEntity, typename Func>
    SelectBuilder& where_doesnt_have(HasOne<RelEntity> Entity::* rel_ptr, Func&& filter) {
        const auto* desc = schema_.find_relation(rel_ptr);
        if (desc && desc->exists_builder) {
            SelectBuilder<RelEntity> child_builder;
            filter(child_builder);
            auto child_conds = child_builder.conditions();
            auto exists_fn = desc->exists_builder;
            std::string parent_tbl = schema_.table_name();

            Condition c;
            c.conj = Conjunction::And;
            c.custom_compiler = [exists_fn, parent_tbl, child_conds = std::move(child_conds)](
                DatabaseDialect dialect, size_t& param_idx, std::vector<std::string>& out_params) -> std::string {
                return exists_fn(dialect, true, parent_tbl, child_conds, param_idx, out_params);
            };
            conditions_.push_back(std::move(c));
        }
        return *this;
    }

    template <typename RelEntity>
    SelectBuilder& where_doesnt_have(HasOne<RelEntity> Entity::* rel_ptr) {
        return where_doesnt_have(rel_ptr, [](auto&) {});
    }

    // Projections
    SelectBuilder& select() {
        projected_columns_.clear();
        return *this;
    }

    template <typename... Fields>
    SelectBuilder& select(Fields Entity::*... fields) {
        projected_columns_.clear();
        (projected_columns_.push_back(schema_.resolve_column_name(fields)), ...);
        return *this;
    }

    SelectBuilder& select_columns(std::vector<std::string> cols) {
        projected_columns_ = std::move(cols);
        return *this;
    }

    // Where conditions
    template <typename FieldType, typename ValueType>
    SelectBuilder& where(FieldType Entity::* field, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), op, {format_param_value(val)}, nullptr});
        return *this;
    }

    template <typename ValueType>
    SelectBuilder& where(std::string col, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::And, std::move(col), op, {format_param_value(val)}, nullptr});
        return *this;
    }

    template <typename FieldType, typename ValueType>
    SelectBuilder& and_where(FieldType Entity::* field, Op op, const ValueType& val) {
        return where(field, op, val);
    }

    template <typename ValueType>
    SelectBuilder& and_where(std::string col, Op op, const ValueType& val) {
        return where(std::move(col), op, val);
    }

    template <typename FieldType, typename ValueType>
    SelectBuilder& or_where(FieldType Entity::* field, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::Or, schema_.resolve_column_name(field), op, {format_param_value(val)}, nullptr});
        return *this;
    }

    template <typename ValueType>
    SelectBuilder& or_where(std::string col, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::Or, std::move(col), op, {format_param_value(val)}, nullptr});
        return *this;
    }

    template <typename FieldType, typename Container>
    SelectBuilder& where_in(FieldType Entity::* field, const Container& values) {
        std::vector<std::string> formatted;
        for (const auto& item : values) {
            formatted.push_back(format_param_value(item));
        }
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), Op::In, std::move(formatted), nullptr});
        return *this;
    }

    template <typename FieldType, typename LowType, typename HighType>
    SelectBuilder& where_between(FieldType Entity::* field, const LowType& low, const HighType& high) {
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), Op::Between, {format_param_value(low), format_param_value(high)}, nullptr});
        return *this;
    }

    template <typename FieldType>
    SelectBuilder& where_null(FieldType Entity::* field) {
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), Op::IsNull, {}, nullptr});
        return *this;
    }

    template <typename FieldType>
    SelectBuilder& where_not_null(FieldType Entity::* field) {
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), Op::IsNotNull, {}, nullptr});
        return *this;
    }

    // Group By
    template <typename FieldType>
    SelectBuilder& group_by(FieldType Entity::* field) {
        group_bys_.push_back(schema_.resolve_column_name(field));
        return *this;
    }

    SelectBuilder& group_by(std::string col) {
        group_bys_.push_back(std::move(col));
        return *this;
    }

    template <typename... Fields>
    SelectBuilder& group_by_fields(Fields Entity::*... fields) {
        (group_bys_.push_back(schema_.resolve_column_name(fields)), ...);
        return *this;
    }

    // Having
    SelectBuilder& having(std::string raw_expr) {
        Condition c;
        c.conj = Conjunction::And;
        c.custom_compiler = [expr = std::move(raw_expr)](DatabaseDialect, size_t&, std::vector<std::string>&) {
            return expr;
        };
        havings_.push_back(std::move(c));
        return *this;
    }

    template <typename FieldType, typename ValueType>
    SelectBuilder& having(FieldType Entity::* field, Op op, const ValueType& val) {
        havings_.push_back({Conjunction::And, schema_.resolve_column_name(field), op, {format_param_value(val)}, nullptr});
        return *this;
    }

    // Order By
    template <typename FieldType>
    SelectBuilder& order_by(FieldType Entity::* field, SortOrder dir = SortOrder::Asc) {
        order_bys_.push_back({schema_.resolve_column_name(field), dir});
        return *this;
    }

    SelectBuilder& order_by(std::string col, SortOrder dir = SortOrder::Asc) {
        order_bys_.push_back({std::move(col), dir});
        return *this;
    }

    SelectBuilder& limit(size_t count) {
        limit_ = count;
        return *this;
    }

    SelectBuilder& offset(size_t count) {
        offset_ = count;
        return *this;
    }

    // SQL compilation
    [[nodiscard]] QueryResult to_sql(DatabaseDialect dialect) const {
        QueryResult result;
        std::string& sql = result.sql;
        sql.reserve(256);

        sql.append("SELECT ");
        if (projected_columns_.empty()) {
            const auto& cols = schema_.columns();
            for (size_t i = 0; i < cols.size(); ++i) {
                sql.append(DialectTraits::quote_identifier(dialect, cols[i].column_name));
                if (i + 1 < cols.size()) sql.append(", ");
            }
        } else {
            for (size_t i = 0; i < projected_columns_.size(); ++i) {
                sql.append(DialectTraits::quote_identifier(dialect, projected_columns_[i]));
                if (i + 1 < projected_columns_.size()) sql.append(", ");
            }
        }

        sql.append(" FROM ");
        sql.append(DialectTraits::quote_identifier(dialect, schema_.table_name()));

        size_t param_idx = 1;

        if (!conditions_.empty()) {
            sql.append(" WHERE ");
            compile_conditions(conditions_, dialect, param_idx, sql, result.params);
        }

        if (!group_bys_.empty()) {
            sql.append(" GROUP BY ");
            for (size_t i = 0; i < group_bys_.size(); ++i) {
                if (i > 0) sql.append(", ");
                sql.append(DialectTraits::quote_identifier(dialect, group_bys_[i]));
            }
        }

        if (!havings_.empty()) {
            sql.append(" HAVING ");
            for (size_t i = 0; i < havings_.size(); ++i) {
                if (i > 0) {
                    sql.append(havings_[i].conj == Conjunction::Or ? " OR " : " AND ");
                }
                compile_condition(havings_[i], dialect, param_idx, sql, result.params);
            }
        }

        if (!order_bys_.empty()) {
            sql.append(" ORDER BY ");
            for (size_t i = 0; i < order_bys_.size(); ++i) {
                sql.append(DialectTraits::quote_identifier(dialect, order_bys_[i].column));
                sql.push_back(' ');
                sql.append(order_to_sql(order_bys_[i].direction));
                if (i + 1 < order_bys_.size()) sql.append(", ");
            }
        }

        if (limit_.has_value()) {
            sql.append(" LIMIT ");
            sql.append(std::to_string(*limit_));
        }

        if (offset_.has_value()) {
            sql.append(" OFFSET ");
            sql.append(std::to_string(*offset_));
        }

        sql.push_back(';');
        return result;
    }

    [[nodiscard]] QueryResult to_count_sql(DatabaseDialect dialect) const {
        QueryResult result;
        std::string& sql = result.sql;
        sql.reserve(128);

        sql.append("SELECT COUNT(*) FROM ");
        sql.append(DialectTraits::quote_identifier(dialect, schema_.table_name()));

        size_t param_idx = 1;

        if (!conditions_.empty()) {
            sql.append(" WHERE ");
            compile_conditions(conditions_, dialect, param_idx, sql, result.params);
        }

        if (!group_bys_.empty()) {
            sql.append(" GROUP BY ");
            for (size_t i = 0; i < group_bys_.size(); ++i) {
                if (i > 0) sql.append(", ");
                sql.append(DialectTraits::quote_identifier(dialect, group_bys_[i]));
            }
        }

        if (!havings_.empty()) {
            sql.append(" HAVING ");
            for (size_t i = 0; i < havings_.size(); ++i) {
                if (i > 0) sql.append(havings_[i].conj == Conjunction::Or ? " OR " : " AND ");
                compile_condition(havings_[i], dialect, param_idx, sql, result.params);
            }
        }

        sql.push_back(';');
        return result;
    }

    [[nodiscard]] QueryResult to_aggregate_sql(DatabaseDialect dialect, std::string_view agg_func, std::string_view col_expr) const {
        QueryResult result;
        std::string& sql = result.sql;
        sql.reserve(128);

        sql.append("SELECT ");
        sql.append(agg_func);
        sql.push_back('(');
        if (col_expr == "*") {
            sql.push_back('*');
        } else {
            sql.append(DialectTraits::quote_identifier(dialect, col_expr));
        }
        sql.append(") FROM ");
        sql.append(DialectTraits::quote_identifier(dialect, schema_.table_name()));

        size_t param_idx = 1;

        if (!conditions_.empty()) {
            sql.append(" WHERE ");
            compile_conditions(conditions_, dialect, param_idx, sql, result.params);
        }

        if (!group_bys_.empty()) {
            sql.append(" GROUP BY ");
            for (size_t i = 0; i < group_bys_.size(); ++i) {
                if (i > 0) sql.append(", ");
                sql.append(DialectTraits::quote_identifier(dialect, group_bys_[i]));
            }
        }

        if (!havings_.empty()) {
            sql.append(" HAVING ");
            for (size_t i = 0; i < havings_.size(); ++i) {
                if (i > 0) sql.append(havings_[i].conj == Conjunction::Or ? " OR " : " AND ");
                compile_condition(havings_[i], dialect, param_idx, sql, result.params);
            }
        }

        sql.push_back(';');
        return result;
    }

    template <typename FieldType>
    [[nodiscard]] QueryResult to_aggregate_sql(DatabaseDialect dialect, std::string_view agg_func, FieldType Entity::* field) const {
        return to_aggregate_sql(dialect, agg_func, schema_.resolve_column_name(field));
    }

    // Auto-mapping: Row -> Entity Hydration
    [[nodiscard]] Entity map_row(const RowView& row) const {
        return schema_.map_row(row);
    }

    template <typename RowContainer>
    [[nodiscard]] std::vector<Entity> map_rows(const RowContainer& rows) const {
        std::vector<Entity> results;
        results.reserve(rows.size());
        for (const auto& row : rows) {
            if constexpr (std::is_pointer_v<std::decay_t<decltype(row)>>) {
                results.push_back(schema_.map_row(*row));
            } else {
                results.push_back(schema_.map_row(row));
            }
        }
        return results;
    }
};

template <typename Entity>
inline SelectBuilder<Entity> from() {
    return SelectBuilder<Entity>();
}

} // namespace aegon::data::orm::sql
