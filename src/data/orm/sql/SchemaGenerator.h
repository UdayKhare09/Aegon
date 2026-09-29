#pragma once

#include "Dialect.h"
#include "Table.h"
#include <string>
#include <string_view>
#include <vector>

#include <span>

namespace aegon::data::orm::sql {

std::string generate_ddl_impl(
    DatabaseDialect dialect,
    std::string_view table_name,
    std::span<const ColumnMetadata> columns,
    std::span<const TableConstraint> constraints,
    std::span<const std::string> composite_pk_columns
);

std::vector<std::string> generate_indexes_impl(
    DatabaseDialect dialect,
    std::string_view table_name,
    std::span<const ColumnMetadata> columns,
    std::span<const TableConstraint> constraints
);

std::string generate_drop_table_impl(
    DatabaseDialect dialect,
    std::string_view table_name,
    bool if_exists,
    bool cascade
);

template <typename Entity>
inline std::string generate_ddl(DatabaseDialect dialect) {
    auto schema = Entity::schema();
    return generate_ddl_impl(dialect, schema.table_name(), schema.columns(), schema.constraints(), schema.composite_pk_columns());
}

template <typename Entity>
inline std::vector<std::string> generate_indexes(DatabaseDialect dialect) {
    auto schema = Entity::schema();
    return generate_indexes_impl(dialect, schema.table_name(), schema.columns(), schema.constraints());
}

template <typename Entity>
inline std::string generate_drop_table(DatabaseDialect dialect, bool if_exists = true, bool cascade = false) {
    auto schema = Entity::schema();
    return generate_drop_table_impl(dialect, schema.table_name(), if_exists, cascade);
}

} // namespace aegon::data::orm::sql
