#pragma once

#include "Dialect.h"
#include "Table.h"
#include "Expression.h"
#include "QueryResult.h"
#include <string>
#include <vector>

namespace aegon::data::orm::sql {

template <typename Entity>
class UpdateBuilder {
    TableDef<Entity> schema_;
    std::vector<std::pair<std::string, std::string>> updates_;
    std::vector<Condition> conditions_;

public:
    UpdateBuilder() : schema_(Entity::schema()) {}
    explicit UpdateBuilder(TableDef<Entity> schema) : schema_(std::move(schema)) {}

    template <typename FieldType, typename ValueType>
    UpdateBuilder& set(FieldType Entity::* field, const ValueType& val) {
        updates_.emplace_back(schema_.resolve_column_name(field), format_param_value(val));
        return *this;
    }

    template <typename ValueType>
    UpdateBuilder& set(std::string col, const ValueType& val) {
        updates_.emplace_back(std::move(col), format_param_value(val));
        return *this;
    }

    UpdateBuilder& set_entity(const Entity& entity) {
        auto extracted = schema_.extract_values(entity, true);
        for (auto& [col, val] : extracted) {
            updates_.emplace_back(std::move(col), std::move(val));
        }
        return *this;
    }

    template <typename FieldType, typename ValueType>
    UpdateBuilder& where(FieldType Entity::* field, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::And, schema_.resolve_column_name(field), op, {format_param_value(val)}});
        return *this;
    }

    template <typename ValueType>
    UpdateBuilder& where(std::string col, Op op, const ValueType& val) {
        conditions_.push_back({Conjunction::And, std::move(col), op, {format_param_value(val)}});
        return *this;
    }

    template <typename FieldType, typename ValueType>
    UpdateBuilder& and_where(FieldType Entity::* field, Op op, const ValueType& val) {
        return where(field, op, val);
    }

    template <typename ValueType>
    UpdateBuilder& and_where(std::string col, Op op, const ValueType& val) {
        return where(std::move(col), op, val);
    }

    [[nodiscard]] QueryResult to_sql(DatabaseDialect dialect) const {
        QueryResult result;
        if (updates_.empty()) {
            return result;
        }

        std::string& sql = result.sql;
        sql.reserve(256);

        sql.append("UPDATE ");
        sql.append(DialectTraits::quote_identifier(dialect, schema_.table_name()));
        sql.append(" SET ");

        size_t param_idx = 1;

        for (size_t i = 0; i < updates_.size(); ++i) {
            sql.append(DialectTraits::quote_identifier(dialect, updates_[i].first));
            sql.append(" = ");
            std::string p;
            DialectTraits::format_placeholder(dialect, param_idx++, p);
            sql.append(p);
            if (i + 1 < updates_.size()) sql.append(", ");
            result.params.push_back(updates_[i].second);
        }

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
inline UpdateBuilder<Entity> update() {
    return UpdateBuilder<Entity>();
}

} // namespace aegon::data::orm::sql
