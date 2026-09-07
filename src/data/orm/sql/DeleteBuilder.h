#pragma once

#include "Dialect.h"
#include "Table.h"
#include "Expression.h"
#include "QueryResult.h"
#include <string>
#include <vector>

namespace aegon::data::orm::sql {

template <typename Entity>
class DeleteBuilder {
    TableDef<Entity> schema_;
    std::vector<Condition> conditions_;

public:
    DeleteBuilder() : schema_(Entity::schema()) {}
    explicit DeleteBuilder(TableDef<Entity> schema) : schema_(std::move(schema)) {}

    template <typename FieldType, typename ValueType>
    DeleteBuilder& where(FieldType Entity::* field, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), op, {format_param_value(val)}});
        return *this;
    }

    template <typename ValueType>
    DeleteBuilder& where(std::string col, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::And, std::move(col), op, {format_param_value(val)}});
        return *this;
    }

    template <typename FieldType, typename ValueType>
    DeleteBuilder& and_where(FieldType Entity::* field, Op op, const ValueType& val) {
        return where(field, op, val);
    }

    template <typename ValueType>
    DeleteBuilder& and_where(std::string col, Op op, const ValueType& val) {
        return where(std::move(col), op, val);
    }

    template <typename FieldType, typename ValueType>
    DeleteBuilder& or_where(FieldType Entity::* field, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::Or, schema_.resolve_column_name(field), op, {format_param_value(val)}});
        return *this;
    }

    template <typename ValueType>
    DeleteBuilder& or_where(std::string col, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::Or, std::move(col), op, {format_param_value(val)}});
        return *this;
    }

    [[nodiscard]] QueryResult to_sql(DatabaseDialect dialect) const {
        QueryResult result;
        std::string& sql = result.sql;
        sql.reserve(128);

        sql.append("DELETE FROM ");
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
                sql.append(op_to_sql(cond.op));
                sql.push_back(' ');
                std::string p;
                DialectTraits::format_placeholder(dialect, param_idx++, p);
                sql.append(p);
                result.params.push_back(cond.values[0]);
            }
        }

        sql.push_back(';');
        return result;
    }
};

template <typename Entity>
inline DeleteBuilder<Entity> delete_from() {
    return DeleteBuilder<Entity>();
}

} // namespace aegon::data::orm::sql
