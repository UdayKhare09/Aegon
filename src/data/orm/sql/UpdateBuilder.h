#pragma once

#include "Dialect.h"
#include "Table.h"
#include "Expression.h"
#include "WhereClauseBuilder.h"
#include "QueryResult.h"
#include "SqlParam.h"
#include <string>
#include <vector>

namespace aegon::data::orm::sql {

template <typename Entity>
class UpdateBuilder : public WhereClauseBuilder<UpdateBuilder<Entity>, Entity> {
    TableDef<Entity> schema_;
    std::vector<std::pair<std::string, SqlParam>> updates_;

public:
    UpdateBuilder() : schema_(Entity::schema()) {}
    explicit UpdateBuilder(TableDef<Entity> schema) : schema_(std::move(schema)) {}

    [[nodiscard]] const TableDef<Entity>& schema() const noexcept { return schema_; }

    template <typename FieldType, typename ValueType>
    UpdateBuilder& set(FieldType Entity::* field, const ValueType& val) {
        updates_.emplace_back(schema_.resolve_column_name(field), make_sql_param(val));
        return *this;
    }

    template <typename ValueType>
    UpdateBuilder& set(std::string col, const ValueType& val) {
        updates_.emplace_back(std::move(col), make_sql_param(val));
        return *this;
    }

    UpdateBuilder& set_entity(const Entity& entity) {
        auto extracted = schema_.extract_values(entity, true);
        for (auto& [col, val] : extracted) {
            updates_.emplace_back(std::move(col), std::move(val));
        }
        return *this;
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

        if (!this->conditions_.empty()) {
            sql.append(" WHERE ");
            compile_conditions(this->conditions_, dialect, param_idx, sql, result.params);
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
