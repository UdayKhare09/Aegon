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
class DeleteBuilder : public WhereClauseBuilder<DeleteBuilder<Entity>, Entity> {
    TableDef<Entity> schema_;

public:
    DeleteBuilder() : schema_(Entity::schema()) {}
    explicit DeleteBuilder(TableDef<Entity> schema) : schema_(std::move(schema)) {}

    [[nodiscard]] const TableDef<Entity>& schema() const noexcept { return schema_; }

    /**
     * @brief Build DELETE SQL and ordered parameters without executing it.
     * An empty condition list omits WHERE, targeting every row if executed.
     */
    [[nodiscard]] QueryResult to_sql(DatabaseDialect dialect) const {
        QueryResult result;
        std::string& sql = result.sql;
        sql.reserve(128);

        sql.append("DELETE FROM ");
        sql.append(DialectTraits::quote_identifier(dialect, schema_.table_name()));

        size_t param_idx = 1;

        if (!this->conditions_.empty()) {
            sql.append(" WHERE ");
            compile_conditions(this->conditions_, dialect, param_idx, sql, result.params);
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
