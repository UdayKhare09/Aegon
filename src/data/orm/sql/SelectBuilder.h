#pragma once

#include "Dialect.h"
#include "Table.h"
#include "Expression.h"
#include "QueryResult.h"
#include "RowView.h"
#include "SqlParam.h"
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

QueryResult compile_select_query_impl(
    DatabaseDialect dialect,
    std::string_view table_name,
    std::string_view select_all_prefix,
    std::span<const std::string> projected_columns,
    std::span<const Condition> conditions,
    std::span<const std::string> group_bys,
    std::span<const Condition> havings,
    std::span<const OrderByClause> order_bys,
    std::optional<size_t> limit,
    std::optional<size_t> offset
);

QueryResult compile_count_query_impl(
    DatabaseDialect dialect,
    std::string_view table_name,
    std::span<const Condition> conditions,
    std::span<const std::string> group_bys,
    std::span<const Condition> havings
);

QueryResult compile_aggregate_query_impl(
    DatabaseDialect dialect,
    std::string_view table_name,
    std::string_view agg_func,
    std::string_view col_expr,
    std::span<const Condition> conditions,
    std::span<const std::string> group_bys,
    std::span<const Condition> havings
);

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
        partition_value_ = make_sql_param(v).to_debug_string();
        return *this;
    }

    [[nodiscard]] std::optional<std::string> resolve_partition_value() const {
        if (partition_value_) return partition_value_;
        if (schema_.cache_config().partition_column) {
            const auto& target_col = *schema_.cache_config().partition_column;
            for (const auto& cond : conditions_) {
                if (cond.conj == Conjunction::And && cond.column == target_col && cond.op == Op::Eq && !cond.values.empty()) {
                    return cond.values[0].to_debug_string();
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
                preds.emplace_back(cond.column, cond.values[0].to_debug_string());
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
            for (char c : p.to_debug_string()) {
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
                DatabaseDialect dialect, size_t& param_idx, std::vector<SqlParam>& out_params) -> std::string {
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
                DatabaseDialect dialect, size_t& param_idx, std::vector<SqlParam>& out_params) -> std::string {
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
                DatabaseDialect dialect, size_t& param_idx, std::vector<SqlParam>& out_params) -> std::string {
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
                DatabaseDialect dialect, size_t& param_idx, std::vector<SqlParam>& out_params) -> std::string {
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
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), op, {make_sql_param(val)}, nullptr});
        return *this;
    }

    template <typename ValueType>
    SelectBuilder& where(std::string col, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::And, std::move(col), op, {make_sql_param(val)}, nullptr});
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
        conditions_.push_back({Conjunction::Or, schema_.resolve_column_name(field), op, {make_sql_param(val)}, nullptr});
        return *this;
    }

    template <typename ValueType>
    SelectBuilder& or_where(std::string col, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::Or, std::move(col), op, {make_sql_param(val)}, nullptr});
        return *this;
    }

    template <typename FieldType, typename Container>
    SelectBuilder& where_in(FieldType Entity::* field, const Container& values) {
        std::vector<SqlParam> formatted;
        for (const auto& item : values) {
            formatted.push_back(make_sql_param(item));
        }
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), Op::In, std::move(formatted), nullptr});
        return *this;
    }

    template <typename FieldType, typename LowType, typename HighType>
    SelectBuilder& where_between(FieldType Entity::* field, const LowType& low, const HighType& high) {
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), Op::Between, {make_sql_param(low), make_sql_param(high)}, nullptr});
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
        c.custom_compiler = [expr = std::move(raw_expr)](DatabaseDialect, size_t&, std::vector<SqlParam>&) {
            return expr;
        };
        havings_.push_back(std::move(c));
        return *this;
    }

    template <typename FieldType, typename ValueType>
    SelectBuilder& having(FieldType Entity::* field, Op op, const ValueType& val) {
        havings_.push_back({Conjunction::And, schema_.resolve_column_name(field), op, {make_sql_param(val)}, nullptr});
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
        return compile_select_query_impl(
            dialect,
            schema_.table_name(),
            schema_.select_all_prefix(dialect),
            projected_columns_,
            conditions_,
            group_bys_,
            havings_,
            order_bys_,
            limit_,
            offset_
        );
    }

    [[nodiscard]] QueryResult to_count_sql(DatabaseDialect dialect) const {
        return compile_count_query_impl(
            dialect,
            schema_.table_name(),
            conditions_,
            group_bys_,
            havings_
        );
    }

    [[nodiscard]] QueryResult to_aggregate_sql(DatabaseDialect dialect, std::string_view agg_func, std::string_view col_expr) const {
        return compile_aggregate_query_impl(
            dialect,
            schema_.table_name(),
            agg_func,
            col_expr,
            conditions_,
            group_bys_,
            havings_
        );
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

    template <typename RowContainer>
    void map_rows_into(const RowContainer& rows, std::vector<Entity>& out) const {
        out.clear();
        out.reserve(rows.size());
        for (const auto& row : rows) {
            if constexpr (std::is_pointer_v<std::decay_t<decltype(row)>>) {
                out.push_back(schema_.map_row(*row));
            } else {
                out.push_back(schema_.map_row(row));
            }
        }
    }
};

template <typename Entity>
inline SelectBuilder<Entity> from() {
    return SelectBuilder<Entity>();
}

} // namespace aegon::data::orm::sql
