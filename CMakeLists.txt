cmake_minimum_required(VERSION 3.20)
project(Aegon LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Enable aggressive optimization and modern x86 instruction sets
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -O3
        -march=native
        -mavx512f
        -mavx512dq
        -mavx512bw
        -mavx512vl
        -mavx512vbmi
        -mavx512vbmi2
        -mvaes
        -mrdrnd
        -mrdseed
        -mmovbe
        -mbmi2
        -msse4.2
        -Wall
        -Wextra
        -Wpedantic
        -Wno-missing-field-initializers
        -Wno-missing-braces
    )
endif()

# UUID library
add_library(aegon_uuid
    src/data/uuid/UUID.cpp
    src/data/uuid/UUIDGenerator.cpp
)

target_include_directories(aegon_uuid PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Core Async io_uring library
add_library(aegon_core
    src/core/BufferPool.cpp
    src/core/IoUring.cpp
    src/core/EventLoop.cpp
)

target_include_directories(aegon_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(aegon_core PUBLIC
    uring
    pthread
)

# Third-party dependencies
include(FetchContent)
FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG v8.3.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glaze)

# Redis Client library
add_library(aegon_redis
    src/data/redis/Resp3.cpp
    src/data/redis/RedisConnection.cpp
    src/data/redis/RedisClient.cpp
    src/data/redis/RedisLock.cpp
)

target_include_directories(aegon_redis PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(aegon_redis PUBLIC
    aegon_core
    aegon_uuid
    crypto
)

# HTTP Server and Routing library
add_library(aegon_http
    src/http/RadixTree.cpp
    src/http/Router.cpp
    src/http/Server.cpp
    src/http/v2/Http2Connection.cpp
    src/http/tls/TlsContext.cpp
    src/http/tls/TlsStream.cpp
    src/http/v3/Http3Connection.cpp
    src/http/v3/Http3Server.cpp
)

target_include_directories(aegon_http PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(aegon_http PUBLIC
    aegon_core
    glaze::glaze
    nghttp2
    ngtcp2_crypto_ossl
    ngtcp2
    nghttp3
    ssl
    crypto
)

# Test and Benchmark executables
add_executable(test_uuid
    tests/test_uuid.cpp
)
target_link_libraries(test_uuid PRIVATE aegon_uuid)

add_executable(test_async_core
    tests/test_async_core.cpp
)
target_link_libraries(test_async_core PRIVATE aegon_core aegon_uuid)

add_executable(test_http_server
    tests/test_http_server.cpp
)
target_link_libraries(test_http_server PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_http2_server
    tests/test_http2_server.cpp
)
target_link_libraries(test_http2_server PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_tls_server
    tests/test_tls_server.cpp
)
target_link_libraries(test_tls_server PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_http3_server
    tests/test_http3_server.cpp
)
target_link_libraries(test_http3_server PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_http3_compliance
    tests/test_http3_compliance.cpp
)
target_link_libraries(test_http3_compliance PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_radix_router
    tests/test_radix_router.cpp
)
target_link_libraries(test_radix_router PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_dto_validation
    tests/test_dto_validation.cpp
)
target_link_libraries(test_dto_validation PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_foundational_types
    tests/test_foundational_types.cpp
)
target_link_libraries(test_foundational_types PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_sql_schema
    tests/test_sql_schema.cpp
)
target_link_libraries(test_sql_schema PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_sql_query_builder
    tests/test_sql_query_builder.cpp
)
target_link_libraries(test_sql_query_builder PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_sql_transaction
    tests/test_sql_transaction.cpp
)
target_link_libraries(test_sql_transaction PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_sql_drivers
    tests/test_sql_drivers.cpp
)
target_link_libraries(test_sql_drivers PRIVATE aegon_http aegon_core aegon_uuid sqlite3 pq)

add_executable(test_live_postgres
    tests/test_live_postgres.cpp
)
target_link_libraries(test_live_postgres PRIVATE aegon_http aegon_core aegon_uuid sqlite3 pq)

add_executable(test_server_sql_config
    tests/test_server_sql_config.cpp
)
target_link_libraries(test_server_sql_config PRIVATE aegon_http aegon_core aegon_uuid sqlite3 pq)

add_executable(test_sql_relations
    tests/test_sql_relations.cpp
)
target_link_libraries(test_sql_relations PRIVATE aegon_http aegon_core aegon_uuid sqlite3 pq)

add_executable(test_sql_advanced_queries
    tests/test_sql_advanced_queries.cpp
)
target_link_libraries(test_sql_advanced_queries PRIVATE aegon_http aegon_core aegon_uuid sqlite3 pq)

add_executable(test_simd_router
    tests/test_simd_router.cpp
)
target_link_libraries(test_simd_router PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_advanced_io
    tests/test_advanced_io.cpp
)
target_link_libraries(test_advanced_io PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_redis_protocol
    tests/test_redis_protocol.cpp
)
target_link_libraries(test_redis_protocol PRIVATE aegon_redis aegon_core)

add_executable(test_orm_cache
    tests/test_orm_cache.cpp
)
target_link_libraries(test_orm_cache PRIVATE aegon_http aegon_core aegon_uuid sqlite3 pq)

add_executable(test_live_redis_topologies
    tests/test_live_redis_topologies.cpp
)
target_link_libraries(test_live_redis_topologies PRIVATE aegon_redis aegon_core)

add_executable(test_live_postgres_orm_cache
    tests/test_live_postgres_orm_cache.cpp
)
target_link_libraries(test_live_postgres_orm_cache PRIVATE aegon_http aegon_redis aegon_core aegon_uuid sqlite3 pq)

add_executable(test_redis_advanced
    tests/test_redis_advanced.cpp
)
target_link_libraries(test_redis_advanced PRIVATE aegon_redis aegon_core crypto)

add_executable(test_orm_advanced
    tests/test_orm_advanced.cpp
)
target_link_libraries(test_orm_advanced PRIVATE aegon_http aegon_core aegon_uuid sqlite3 pq)

add_executable(test_sql_tx_cache_parity
    tests/test_sql_tx_cache_parity.cpp
)
target_link_libraries(test_sql_tx_cache_parity PRIVATE aegon_http aegon_core aegon_uuid sqlite3 pq)

add_executable(test_error_handling
    tests/test_error_handling.cpp
)
target_link_libraries(test_error_handling PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_middleware
    tests/test_middleware.cpp
)
target_link_libraries(test_middleware PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_cors
    tests/test_cors.cpp
)
target_link_libraries(test_cors PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_security_headers
    tests/test_security_headers.cpp
)
target_link_libraries(test_security_headers PRIVATE aegon_http aegon_core aegon_uuid)

add_executable(test_cookie
    tests/test_cookie.cpp
)
target_link_libraries(test_cookie PRIVATE aegon_http aegon_core aegon_uuid)

# Test Registrations for CTest
add_test(NAME foundational_types COMMAND test_foundational_types)
add_test(NAME dto_validation COMMAND test_dto_validation)
add_test(NAME simd_router COMMAND test_simd_router)
add_test(NAME radix_router COMMAND test_radix_router)
add_test(NAME advanced_io COMMAND test_advanced_io)
add_test(NAME redis_protocol COMMAND test_redis_protocol)
add_test(NAME sql_schema COMMAND test_sql_schema)
add_test(NAME sql_query_builder COMMAND test_sql_query_builder)
add_test(NAME sql_transaction COMMAND test_sql_transaction)
add_test(NAME sql_relations COMMAND test_sql_relations)
add_test(NAME sql_advanced_queries COMMAND test_sql_advanced_queries)
add_test(NAME sql_drivers COMMAND test_sql_drivers)
add_test(NAME server_sql_config COMMAND test_server_sql_config)
add_test(NAME orm_cache COMMAND test_orm_cache)
add_test(NAME live_postgres COMMAND test_live_postgres)
add_test(NAME live_redis_topologies COMMAND test_live_redis_topologies)
add_test(NAME live_postgres_orm_cache COMMAND test_live_postgres_orm_cache)
add_test(NAME redis_advanced COMMAND test_redis_advanced)
add_test(NAME orm_advanced COMMAND test_orm_advanced)
add_test(NAME sql_tx_cache_parity COMMAND test_sql_tx_cache_parity)
add_test(NAME error_handling COMMAND test_error_handling)
add_test(NAME middleware COMMAND test_middleware)
add_test(NAME cors COMMAND test_cors)
add_test(NAME security_headers COMMAND test_security_headers)
add_test(NAME cookie COMMAND test_cookie)

# Samples
add_subdirectory(samples/adv)
