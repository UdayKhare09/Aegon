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
    glaze::glaze
    ${AEGON_SQLITE_LIB}
    ${AEGON_POSTGRES_LIB}
)

if(PostgreSQL_INCLUDE_DIRS)
    target_include_directories(aegon_orm PUBLIC ${PostgreSQL_INCLUDE_DIRS})
endif()
if(SQLite3_INCLUDE_DIRS)
    target_include_directories(aegon_orm PUBLIC ${SQLite3_INCLUDE_DIRS})
endif()
