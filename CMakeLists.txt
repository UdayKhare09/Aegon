cmake_minimum_required(VERSION 3.20)

set(AEGON_VERSION_MAJOR 0)
set(AEGON_VERSION_MINOR 1)
set(AEGON_VERSION_PATCH 0)
set(AEGON_VERSION_PRERELEASE "a2")
set(AEGON_VERSION_STRING "${AEGON_VERSION_MAJOR}.${AEGON_VERSION_MINOR}.${AEGON_VERSION_PRERELEASE}")

project(Aegon
    VERSION ${AEGON_VERSION_MAJOR}.${AEGON_VERSION_MINOR}.${AEGON_VERSION_PATCH}
    LANGUAGES CXX
)

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
        -Wall
        -Wextra
        -Wpedantic
        -Wno-missing-field-initializers
        -Wno-missing-braces
    )
endif()

include(GNUInstallDirs)

# Third-party core dependencies
find_package(Threads REQUIRED)

# Core Async io_uring library
add_library(aegon_core
    src/core/BufferPool.cpp
    src/core/IoUring.cpp
    src/core/EventLoop.cpp
)

target_include_directories(aegon_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/aegon>
)

target_link_libraries(aegon_core PUBLIC
    uring
    Threads::Threads
)

# Third-party dependencies: Glaze (system package)
find_package(glaze REQUIRED)

# Configuration library (YAML, Env, CLI)
add_library(aegon_config
    src/config/DotEnv.cpp
    src/config/EnvExpander.cpp
)

target_include_directories(aegon_config PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/aegon>
)

target_link_libraries(aegon_config PUBLIC
    glaze::glaze
)

# Redis Client library
add_library(aegon_redis
    src/data/redis/Resp3.cpp
    src/data/redis/RedisConnection.cpp
    src/data/redis/RedisClient.cpp
    src/data/redis/RedisLock.cpp
)

target_include_directories(aegon_redis PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/aegon>
)

target_link_libraries(aegon_redis PUBLIC
    aegon_core
    crypto
)

# Async in-process MemStore (alternative to Redis for single-node deployments)
add_library(aegon_memory
    src/data/memory/MemStore.cpp
)

target_include_directories(aegon_memory PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/aegon>
)

target_link_libraries(aegon_memory PUBLIC
    aegon_core
    uring
)

# Find database drivers for unified ORM
find_package(PostgreSQL QUIET)

# Unified ORM (Object-Relational Mapping & SQL Query Engine) library
add_library(aegon_orm INTERFACE)

target_include_directories(aegon_orm INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/aegon>
    $<$<BOOL:${PostgreSQL_FOUND}>:${PostgreSQL_INCLUDE_DIRS}>
)

target_link_libraries(aegon_orm INTERFACE
    aegon_core
    glaze::glaze
    sqlite3
    pq
)

find_library(LIBDEFLATE_LIB deflate REQUIRED)

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
    src/http/client/TlsClientStream.cpp
    src/http/client/ConnectionPool.cpp
    src/http/client/HttpClient.cpp
    src/http/client/Http2ClientSession.cpp
    src/http/client/Http3ClientSession.cpp
    src/http/websocket/WebSocketSession.cpp
    src/http/websocket/WebSocketConnection.cpp
)

target_include_directories(aegon_http PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/aegon>
)

target_link_libraries(aegon_http PUBLIC
    aegon_core
    glaze::glaze
    ${LIBDEFLATE_LIB}
    nghttp2
    ngtcp2_crypto_ossl
    ngtcp2
    nghttp3
    ssl
    crypto
)

# Cloud-Native API Gateway & Reverse Proxy library
add_library(aegon_gateway
    src/gateway/UpstreamNode.cpp
    src/gateway/LoadBalancer.cpp
    src/gateway/CircuitBreaker.cpp
    src/gateway/UpstreamCluster.cpp
    src/gateway/ProxyHandler.cpp
)

target_include_directories(aegon_gateway PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/aegon>
)

target_link_libraries(aegon_gateway PUBLIC
    aegon_http
    aegon_core
)

# Export names and in-tree aliases for modular targets (Aegon::core, Aegon::http, etc.)
set_target_properties(aegon_core PROPERTIES EXPORT_NAME core)
set_target_properties(aegon_config PROPERTIES EXPORT_NAME config)
set_target_properties(aegon_redis PROPERTIES EXPORT_NAME redis)
set_target_properties(aegon_memory PROPERTIES EXPORT_NAME memory)
set_target_properties(aegon_orm PROPERTIES EXPORT_NAME orm)
set_target_properties(aegon_http PROPERTIES EXPORT_NAME http)
set_target_properties(aegon_gateway PROPERTIES EXPORT_NAME gateway)

add_library(Aegon::core ALIAS aegon_core)
add_library(Aegon::config ALIAS aegon_config)
add_library(Aegon::redis ALIAS aegon_redis)
add_library(Aegon::memory ALIAS aegon_memory)
add_library(Aegon::orm ALIAS aegon_orm)
add_library(Aegon::http ALIAS aegon_http)
add_library(Aegon::gateway ALIAS aegon_gateway)

# ==============================================================================
# Installation & Package Configuration (cmake --install & find_package(Aegon))
# ==============================================================================
install(
    TARGETS aegon_core aegon_config aegon_redis aegon_memory aegon_orm aegon_http aegon_gateway
    EXPORT AegonTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/aegon
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

install(
    EXPORT AegonTargets
    FILE AegonTargets.cmake
    NAMESPACE Aegon::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Aegon
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AegonConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/AegonConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Aegon
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/AegonConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/AegonConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/AegonConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Aegon
)

# ==============================================================================
# Aegon Developer CLI Tool (aegon)
# ==============================================================================
add_executable(aegon_cli
    tools/cli/main.cpp
)
set_target_properties(aegon_cli PROPERTIES OUTPUT_NAME aegon)

target_include_directories(aegon_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/cli
)

target_link_libraries(aegon_cli PRIVATE
    Aegon::core
    Aegon::config
)

install(
    TARGETS aegon_cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Test and Benchmark executables
add_executable(test_uuid
    tests/test_uuid.cpp
)
target_link_libraries(test_uuid PRIVATE aegon_core)

add_executable(test_async_core
    tests/test_async_core.cpp
)
target_link_libraries(test_async_core PRIVATE aegon_core)

add_executable(test_http_server
    tests/test_http_server.cpp
)
target_link_libraries(test_http_server PRIVATE aegon_http aegon_core)

add_executable(test_http2_server
    tests/test_http2_server.cpp
)
target_link_libraries(test_http2_server PRIVATE aegon_http aegon_core)

add_executable(test_tls_server
    tests/test_tls_server.cpp
)
target_link_libraries(test_tls_server PRIVATE aegon_http aegon_core)

add_executable(test_http3_server
    tests/test_http3_server.cpp
)
target_link_libraries(test_http3_server PRIVATE aegon_http aegon_core)

add_executable(test_http3_compliance
    tests/test_http3_compliance.cpp
)
target_link_libraries(test_http3_compliance PRIVATE aegon_http aegon_core)

add_executable(test_radix_router
    tests/test_radix_router.cpp
)
target_link_libraries(test_radix_router PRIVATE aegon_http aegon_core)

add_executable(test_dto_validation
    tests/test_dto_validation.cpp
)
target_link_libraries(test_dto_validation PRIVATE aegon_http aegon_core)

add_executable(test_foundational_types
    tests/test_foundational_types.cpp
)
target_link_libraries(test_foundational_types PRIVATE aegon_http aegon_core)

add_executable(test_sql_schema
    tests/test_sql_schema.cpp
)
target_link_libraries(test_sql_schema PRIVATE aegon_orm aegon_http)

add_executable(test_sql_query_builder
    tests/test_sql_query_builder.cpp
)
target_link_libraries(test_sql_query_builder PRIVATE aegon_orm aegon_http)

add_executable(test_sql_transaction
    tests/test_sql_transaction.cpp
)
target_link_libraries(test_sql_transaction PRIVATE aegon_orm aegon_http)

add_executable(test_sql_drivers
    tests/test_sql_drivers.cpp
)
target_link_libraries(test_sql_drivers PRIVATE aegon_orm aegon_http)

add_executable(test_percore_clients
    tests/test_percore_clients.cpp
)
target_link_libraries(test_percore_clients PRIVATE aegon_orm aegon_http)

add_executable(test_live_postgres
    tests/test_live_postgres.cpp
)
target_link_libraries(test_live_postgres PRIVATE aegon_orm aegon_http)

add_executable(test_server_sql_config
    tests/test_server_sql_config.cpp
)
target_link_libraries(test_server_sql_config PRIVATE aegon_orm aegon_http)

add_executable(test_sql_relations
    tests/test_sql_relations.cpp
)
target_link_libraries(test_sql_relations PRIVATE aegon_orm aegon_http)

add_executable(test_sql_advanced_queries
    tests/test_sql_advanced_queries.cpp
)
target_link_libraries(test_sql_advanced_queries PRIVATE aegon_orm aegon_http)

add_executable(test_simd_router
    tests/test_simd_router.cpp
)
target_link_libraries(test_simd_router PRIVATE aegon_http aegon_core)

add_executable(test_advanced_io
    tests/test_advanced_io.cpp
)
target_link_libraries(test_advanced_io PRIVATE aegon_http aegon_core)

add_executable(test_redis_protocol
    tests/test_redis_protocol.cpp
)
target_link_libraries(test_redis_protocol PRIVATE aegon_redis aegon_core)

add_executable(test_orm_cache
    tests/test_orm_cache.cpp
)
target_link_libraries(test_orm_cache PRIVATE aegon_orm aegon_http)

add_executable(test_memstore
    tests/test_memstore.cpp
)
target_link_libraries(test_memstore PRIVATE aegon_memory aegon_core)

add_executable(test_live_redis_topologies
    tests/test_live_redis_topologies.cpp
)
target_link_libraries(test_live_redis_topologies PRIVATE aegon_redis aegon_core)

add_executable(test_live_postgres_orm_cache
    tests/test_live_postgres_orm_cache.cpp
)
target_link_libraries(test_live_postgres_orm_cache PRIVATE aegon_orm aegon_redis aegon_http)

add_executable(test_redis_advanced
    tests/test_redis_advanced.cpp
)
target_link_libraries(test_redis_advanced PRIVATE aegon_redis aegon_core crypto)

add_executable(test_orm_advanced
    tests/test_orm_advanced.cpp
)
target_link_libraries(test_orm_advanced PRIVATE aegon_orm aegon_http)

add_executable(test_sql_tx_cache_parity
    tests/test_sql_tx_cache_parity.cpp
)
target_link_libraries(test_sql_tx_cache_parity PRIVATE aegon_orm aegon_http)

add_executable(test_error_handling
    tests/test_error_handling.cpp
)
target_link_libraries(test_error_handling PRIVATE aegon_http aegon_core)

add_executable(test_middleware
    tests/test_middleware.cpp
)
target_link_libraries(test_middleware PRIVATE aegon_http aegon_core)

add_executable(test_cors
    tests/test_cors.cpp
)
target_link_libraries(test_cors PRIVATE aegon_http aegon_core)

add_executable(test_security_headers
    tests/test_security_headers.cpp
)
target_link_libraries(test_security_headers PRIVATE aegon_http aegon_core)

add_executable(test_cookie
    tests/test_cookie.cpp
)
target_link_libraries(test_cookie PRIVATE aegon_http aegon_core)

add_executable(test_jwt
    tests/test_jwt.cpp
)
target_link_libraries(test_jwt PRIVATE aegon_http aegon_core)

add_executable(test_auth_middleware
    tests/test_auth_middleware.cpp
)
target_link_libraries(test_auth_middleware PRIVATE aegon_http aegon_core)

add_executable(test_rbac_guard
    tests/test_rbac_guard.cpp
)
target_link_libraries(test_rbac_guard PRIVATE aegon_http aegon_core)

add_executable(test_jwks
    tests/test_jwks.cpp
)
target_link_libraries(test_jwks PRIVATE aegon_http aegon_core)

add_executable(test_http_client
    tests/test_http_client.cpp
)
target_link_libraries(test_http_client PRIVATE aegon_http aegon_core)

add_executable(test_service_registry
    tests/test_service_registry.cpp
)
target_link_libraries(test_service_registry PRIVATE aegon_http aegon_core)

add_executable(test_config
    tests/test_config.cpp
)
target_link_libraries(test_config PRIVATE aegon_config)

add_executable(test_gateway
    tests/test_gateway.cpp
)
target_link_libraries(test_gateway PRIVATE aegon_gateway aegon_http aegon_core)

add_executable(test_simd_string
    tests/test_simd_string.cpp
)
target_link_libraries(test_simd_string PRIVATE aegon_core)

add_executable(test_version
    tests/test_version.cpp
)
target_include_directories(test_version PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(test_compress
    tests/test_compress.cpp
)
target_link_libraries(test_compress PRIVATE aegon_http aegon_core ${LIBDEFLATE_LIB})

add_executable(test_websocket
    tests/test_websocket.cpp
)
target_link_libraries(test_websocket PRIVATE aegon_http aegon_core)

# Test Registrations for CTest
add_test(NAME version COMMAND test_version)
add_test(NAME websocket COMMAND test_websocket)
add_test(NAME simd_string COMMAND test_simd_string)
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
add_test(NAME percore_clients COMMAND test_percore_clients)
add_test(NAME server_sql_config COMMAND test_server_sql_config)
add_test(NAME orm_cache COMMAND test_orm_cache)
add_test(NAME memstore COMMAND test_memstore)
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
add_test(NAME compress COMMAND test_compress)
add_test(NAME cookie COMMAND test_cookie)
add_test(NAME jwt COMMAND test_jwt)
add_test(NAME auth_middleware COMMAND test_auth_middleware)
add_test(NAME rbac_guard COMMAND test_rbac_guard)
add_test(NAME jwks COMMAND test_jwks)
add_test(NAME http_client COMMAND test_http_client)
add_test(NAME service_registry COMMAND test_service_registry)
add_test(NAME config COMMAND test_config)
add_test(NAME gateway COMMAND test_gateway)
add_test(NAME cli_version COMMAND aegon version)
add_test(NAME cli_help COMMAND aegon help)
add_test(NAME cli_doctor COMMAND aegon doctor)

# Samples
add_subdirectory(samples/adv)
