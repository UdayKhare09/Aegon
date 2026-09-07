#pragma once

#include "Dialect.h"
#include "Table.h"
#include "QueryResult.h"
#include <string>
#include <vector>

namespace aegon::data::orm::sql {

template <typename Entity>
class InsertBuilder {
    TableDef<Entity> schema_;
    std::vector<Entity> entities_;

public:
    InsertBuilder() : schema_(Entity::schema()) {}
    explicit InsertBuilder(TableDef<Entity> schema) : schema_(std::move(schema)) {}

    InsertBuilder& values(Entity entity) {
        entities_.push_back(std::move(entity));
        return *this;
    }

    InsertBuilder& values(std::vector<Entity> entities) {
        for (auto& e : entities) {
            entities_.push_back(std::move(e));
        }
        return *this;
    }

    [[nodiscard]] QueryResult to_sql(DatabaseDialect dialect) const {
        QueryResult result;
        if (entities_.empty()) {
            return result;
        }

        std::string& sql = result.sql;
        sql.reserve(256);

        sql.append("INSERT INTO ");
        sql.append(DialectTraits::quote_identifier(dialect, schema_.table_name()));
        sql.append(" (");

        // Identify columns to insert (skip auto-increment PKs)
        const auto& all_cols = schema_.columns();
        std::vector<size_t> insert_col_indices;
        bool has_auto_inc_pk = false;

        for (size_t i = 0; i < all_cols.size(); ++i) {
            if (all_cols[i].is_auto_increment) {
                has_auto_inc_pk = true;
                continue;
            }
            insert_col_indices.push_back(i);
        }

        for (size_t i = 0; i < insert_col_indices.size(); ++i) {
            sql.append(DialectTraits::quote_identifier(dialect, all_cols[insert_col_indices[i]].column_name));
            if (i + 1 < insert_col_indices.size()) sql.append(", ");
        }

        sql.append(") VALUES ");

        size_t param_idx = 1;

        for (size_t row_idx = 0; row_idx < entities_.size(); ++row_idx) {
            const auto& entity = entities_[row_idx];
            auto extracted = schema_.extract_values(entity, true);

            sql.push_back('(');
            for (size_t col_i = 0; col_i < extracted.size(); ++col_i) {
                std::string p;
                DialectTraits::format_placeholder(dialect, param_idx++, p);
                sql.append(p);
                if (col_i + 1 < extracted.size()) sql.append(", ");
                result.params.push_back(extracted[col_i].second);
            }
            sql.push_back(')');
            if (row_idx + 1 < entities_.size()) sql.append(", ");
        }

        if (dialect == DatabaseDialect::PostgreSQL && has_auto_inc_pk) {
            sql.append(" RETURNING ");
            sql.append(DialectTraits::quote_identifier(dialect, schema_.primary_key_name()));
        }

        sql.push_back(';');
        return result;
    }
};

template <typename Entity>
inline InsertBuilder<Entity> insert_into() {
    return InsertBuilder<Entity>();
}

} // namespace aegon::data::orm::sql
