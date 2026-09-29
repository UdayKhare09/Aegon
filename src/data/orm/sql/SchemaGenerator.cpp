#include "SchemaGenerator.h"

namespace aegon::data::orm::sql {

std::string generate_ddl_impl(
    DatabaseDialect dialect,
    std::string_view table_name,
    std::span<const ColumnMetadata> columns,
    std::span<const TableConstraint> constraints,
    std::span<const std::string> composite_pk_columns
) {
    bool has_composite_pk = !composite_pk_columns.empty();

    std::string sql = "CREATE TABLE IF NOT EXISTS ";
    sql.append(DialectTraits::quote_identifier(dialect, table_name));
    sql.append(" (\n");

    std::vector<std::string> table_constraints;

    if (has_composite_pk) {
        std::string pk_sql = "    PRIMARY KEY (";
        for (size_t k = 0; k < composite_pk_columns.size(); ++k) {
            if (k > 0) pk_sql.append(", ");
            pk_sql.append(DialectTraits::quote_identifier(dialect, composite_pk_columns[k]));
        }
        pk_sql.push_back(')');
        table_constraints.push_back(std::move(pk_sql));
    }

    for (size_t i = 0; i < columns.size(); ++i) {
        const auto& col = columns[i];
        sql.append("    ");
        sql.append(DialectTraits::quote_identifier(dialect, col.column_name));
        sql.push_back(' ');

        if (col.is_primary_key && col.is_auto_increment && !has_composite_pk) {
            sql.append(DialectTraits::auto_increment_pk(dialect));
        } else {
            sql.append(col.sql_type_fn(dialect, col.length));

            if (col.is_primary_key && !has_composite_pk) {
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
                    sql.append(" DEFAULT ");
                    sql.append(DialectTraits::current_timestamp(dialect));
                } else if (!col.default_value.empty()) {
                    sql.append(" DEFAULT ");
                    sql.append(col.default_value);
                }
            }
        }

        if (col.foreign_key) {
            std::string fk_sql = "    CONSTRAINT fk_" + std::string(table_name) + "_" + col.column_name +
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
            table_constraints.push_back(std::move(fk_sql));
        }

        if (i + 1 < columns.size() || !table_constraints.empty()) {
            sql.append(",\n");
        } else {
            sql.push_back('\n');
        }
    }

    for (const auto& c : constraints) {
        if (c.kind == TableConstraint::Kind::UniqueKey) {
            std::string u_sql = "    CONSTRAINT ";
            std::string cname = c.name.empty() ? ("uq_" + std::string(table_name)) : c.name;
            u_sql.append(cname);
            u_sql.append(" UNIQUE (");
            for (size_t k = 0; k < c.columns.size(); ++k) {
                if (k > 0) u_sql.append(", ");
                u_sql.append(DialectTraits::quote_identifier(dialect, c.columns[k]));
            }
            u_sql.push_back(')');
            table_constraints.push_back(std::move(u_sql));
        }
    }

    for (size_t i = 0; i < table_constraints.size(); ++i) {
        sql.append(table_constraints[i]);
        if (i + 1 < table_constraints.size()) {
            sql.append(",\n");
        } else {
            sql.push_back('\n');
        }
    }

    sql.append(");");
    return sql;
}

std::vector<std::string> generate_indexes_impl(
    DatabaseDialect dialect,
    std::string_view table_name,
    std::span<const ColumnMetadata> columns,
    std::span<const TableConstraint> constraints
) {
    std::vector<std::string> indexes;

    for (const auto& col : columns) {
        if (col.is_indexed && !col.is_primary_key && !col.is_unique) {
            std::string idx = "CREATE INDEX IF NOT EXISTS idx_" + std::string(table_name) + "_" + col.column_name +
                              " ON " + DialectTraits::quote_identifier(dialect, table_name) +
                              " (" + DialectTraits::quote_identifier(dialect, col.column_name) + ");";
            indexes.push_back(std::move(idx));
        }
    }

    for (const auto& c : constraints) {
        if (c.kind == TableConstraint::Kind::Index) {
            std::string idx = "CREATE ";
            if (c.unique) idx.append("UNIQUE ");
            idx.append("INDEX IF NOT EXISTS ");
            std::string iname = c.name;
            if (iname.empty()) {
                iname = "idx_" + std::string(table_name);
                for (const auto& col_name : c.columns) {
                    iname += "_" + col_name;
                }
            }
            idx.append(iname);
            idx.append(" ON ");
            idx.append(DialectTraits::quote_identifier(dialect, table_name));
            idx.append(" (");
            for (size_t k = 0; k < c.columns.size(); ++k) {
                if (k > 0) idx.append(", ");
                idx.append(DialectTraits::quote_identifier(dialect, c.columns[k]));
            }
            idx.append(");");
            indexes.push_back(std::move(idx));
        }
    }

    return indexes;
}

std::string generate_drop_table_impl(
    DatabaseDialect dialect,
    std::string_view table_name,
    bool if_exists,
    bool cascade
) {
    std::string sql = "DROP TABLE ";
    if (if_exists) sql.append("IF EXISTS ");
    sql.append(DialectTraits::quote_identifier(dialect, table_name));
    if (cascade && dialect == DatabaseDialect::PostgreSQL) {
        sql.append(" CASCADE");
    }
    sql.push_back(';');
    return sql;
}

} // namespace aegon::data::orm::sql
