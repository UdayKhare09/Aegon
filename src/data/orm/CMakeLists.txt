# ==============================================================================
# Unified ORM & SQL Query Engine (aegon_orm)
# ==============================================================================

add_library(aegon_orm
    sql/SqlParam.cpp
    sql/Dialect.cpp
    sql/Expression.cpp
    sql/SelectBuilder.cpp
    sql/OrmCache.cpp
    sql/SchemaGenerator.cpp
    sql/PerCoreConnectionPool.cpp
    sql/drivers/SqliteDriver.cpp
    sql/drivers/PostgresDriver.cpp
)

target_link_libraries(aegon_orm PUBLIC
    aegon_core
    Aegon::Ext::glaze
    Aegon::Ext::SQLite3
    Aegon::Ext::PostgreSQL
)
