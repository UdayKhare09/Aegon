#pragma once

#include "Dialect.h"
#include "Table.h"
#include <string>
#include <string_view>
#include <vector>

namespace aegon::data::orm::sql {

template <typename Entity>
inline std::string generate_ddl(DatabaseDialect dialect) {
    auto schema = Entity::schema();
    const auto& table_name = schema.table_name();
    const auto& columns = schema.columns();

    std::string sql = "CREATE TABLE IF NOT EXISTS ";
    sql.append(DialectTraits::quote_identifier(dialect, table_name));
    sql.append(" (\n");

    std::vector<std::string> fk_constraints;

    for (size_t i = 0; i < columns.size(); ++i) {
        const auto& col = columns[i];
        sql.append("    ");
        sql.append(DialectTraits::quote_identifier(dialect, col.column_name));
        sql.push_back(' ');

        if (col.is_primary_key && col.is_auto_increment) {
            sql.append(DialectTraits::auto_increment_pk(dialect));
        } else {
            sql.append(col.sql_type_fn(dialect, col.length));

            if (col.is_primary_key) {
                sql.append(" PRIMARY KEY");
            } else {
                if (!col.is_nullable) {
                    sql.append(" NOT NULL");
                }
                if (col.is_unique) {
                    sql.append(" UNIQUE");
                }
                if (col.is_created_at) {
                    sql.append(" DEFAULT ");
                    sql.append(DialectTraits::current_timestamp(dialect));
                } else if (col.is_updated_at) {
                    if (dialect == DatabaseDialect::MySQL) {
                        sql.append(" DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6)");
                    } else {
                        sql.append(" DEFAULT ");
                        sql.append(DialectTraits::current_timestamp(dialect));
                    }
                } else if (!col.default_value.empty()) {
                    sql.append(" DEFAULT ");
                    sql.append(col.default_value);
                }
            }
        }

        if (col.foreign_key) {
            std::string fk_sql = "    CONSTRAINT fk_" + table_name + "_" + col.column_name +
                                 " FOREIGN KEY (" + DialectTraits::quote_identifier(dialect, col.column_name) +
                                 ") REFERENCES " + DialectTraits::quote_identifier(dialect, col.foreign_key->target_table) +
                                 " (" + DialectTraits::quote_identifier(dialect, col.foreign_key->target_column) + ")";

            if (col.foreign_key->on_delete != OnDeleteAction::NoAction) {
                fk_sql.append(" ON DELETE ");
                fk_sql.append(on_delete_action_to_sql(col.foreign_key->on_delete));
            }
            if (col.foreign_key->on_update != OnDeleteAction::NoAction) {
                fk_sql.append(" ON UPDATE ");
                fk_sql.append(on_delete_action_to_sql(col.foreign_key->on_update));
            }
            fk_constraints.push_back(std::move(fk_sql));
        }

        if (i + 1 < columns.size() || !fk_constraints.empty()) {
            sql.append(",\n");
        } else {
            sql.push_back('\n');
        }
    }

    for (size_t i = 0; i < fk_constraints.size(); ++i) {
        sql.append(fk_constraints[i]);
        if (i + 1 < fk_constraints.size()) {
            sql.append(",\n");
        } else {
            sql.push_back('\n');
        }
    }

    sql.append(");");
    return sql;
}

template <typename Entity>
inline std::vector<std::string> generate_indexes(DatabaseDialect dialect) {
    auto schema = Entity::schema();
    const auto& table_name = schema.table_name();
    const auto& columns = schema.columns();

    std::vector<std::string> indexes;

    for (const auto& col : columns) {
        if (col.is_indexed && !col.is_primary_key && !col.is_unique) {
            std::string idx = "CREATE INDEX IF NOT EXISTS idx_" + table_name + "_" + col.column_name +
                              " ON " + DialectTraits::quote_identifier(dialect, table_name) +
                              " (" + DialectTraits::quote_identifier(dialect, col.column_name) + ");";
            indexes.push_back(std::move(idx));
        }
    }

    return indexes;
}

template <typename Entity>
inline std::string generate_drop_table(DatabaseDialect dialect, bool if_exists = true, bool cascade = false) {
    auto schema = Entity::schema();
    std::string sql = "DROP TABLE ";
    if (if_exists) sql.append("IF EXISTS ");
    sql.append(DialectTraits::quote_identifier(dialect, schema.table_name()));
    if (cascade && dialect == DatabaseDialect::PostgreSQL) {
        sql.append(" CASCADE");
    }
    sql.push_back(';');
    return sql;
}

} // namespace aegon::data::orm::sql
