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

namespace aegon::data::orm::sql {

template <typename Entity>
class SelectBuilder {
    TableDef<Entity> schema_;
    std::vector<std::string> projected_columns_;
    std::vector<Condition> conditions_;
    std::vector<OrderByClause> order_bys_;
    std::optional<size_t> limit_;
    std::optional<size_t> offset_;
    std::vector<std::function<core::Task<void>(std::span<Entity>, Connection&, DatabaseDialect)>> includes_;

public:
    SelectBuilder() : schema_(Entity::schema()) {}
    explicit SelectBuilder(TableDef<Entity> schema) : schema_(std::move(schema)) {}

    template <typename TargetField>
    SelectBuilder& include(TargetField Entity::* rel_ptr) {
        const auto* desc = schema_.find_relation(rel_ptr);
        if (desc && desc->eager_loader) {
            includes_.push_back(desc->eager_loader);
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
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), op, {format_param_value(val)}});
        return *this;
    }

    template <typename ValueType>
    SelectBuilder& where(std::string col, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::And, std::move(col), op, {format_param_value(val)}});
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
        conditions_.push_back({Conjunction::Or, schema_.resolve_column_name(field), op, {format_param_value(val)}});
        return *this;
    }

    template <typename ValueType>
    SelectBuilder& or_where(std::string col, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::Or, std::move(col), op, {format_param_value(val)}});
        return *this;
    }

    template <typename FieldType, typename Container>
    SelectBuilder& where_in(FieldType Entity::* field, const Container& values) {
        std::vector<std::string> formatted;
        for (const auto& item : values) {
            formatted.push_back(format_param_value(item));
        }
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), Op::In, std::move(formatted)});
        return *this;
    }

    template <typename FieldType, typename LowType, typename HighType>
    SelectBuilder& where_between(FieldType Entity::* field, const LowType& low, const HighType& high) {
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), Op::Between, {format_param_value(low), format_param_value(high)}});
        return *this;
    }

    template <typename FieldType>
    SelectBuilder& where_null(FieldType Entity::* field) {
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), Op::IsNull, {}});
        return *this;
    }

    template <typename FieldType>
    SelectBuilder& where_not_null(FieldType Entity::* field) {
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), Op::IsNotNull, {}});
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
            for (size_t i = 0; i < conditions_.size(); ++i) {
                const auto& cond = conditions_[i];
                if (i > 0) {
                    sql.append(cond.conj == Conjunction::Or ? " OR " : " AND ");
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
                    result.params.push_back(cond.values[0]);
                    result.params.push_back(cond.values[1]);
                } else if (cond.op == Op::In || cond.op == Op::NotIn) {
                    sql.append(op_to_sql(cond.op));
                    sql.append(" (");
                    for (size_t j = 0; j < cond.values.size(); ++j) {
                        std::string p;
                        DialectTraits::format_placeholder(dialect, param_idx++, p);
                        sql.append(p);
                        if (j + 1 < cond.values.size()) sql.append(", ");
                        result.params.push_back(cond.values[j]);
                    }
                    sql.push_back(')');
                } else {
                    sql.append(op_to_sql(cond.op));
                    sql.push_back(' ');
                    std::string p;
                    DialectTraits::format_placeholder(dialect, param_idx++, p);
                    sql.append(p);
                    result.params.push_back(cond.values[0]);
                }
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
