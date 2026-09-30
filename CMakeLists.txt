cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Aegon Web Framework & High-Performance C++26 Engine
# ==============================================================================

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

# C++26 Standard
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Optimization & Compiler Flags
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

# ==============================================================================
# Dependencies
# ==============================================================================
find_package(Threads REQUIRED)
find_package(glaze REQUIRED)
find_package(PostgreSQL QUIET)
find_library(LIBDEFLATE_LIB deflate REQUIRED)

# ==============================================================================
# Core Framework Libraries
# ==============================================================================

# 0. High-Performance C++26 Telemetry & Logging (aegon_log)
add_library(aegon_log
    src/log/ConsoleSink.cpp
    src/log/FileSink.cpp
    src/log/Logger.cpp
)
target_link_libraries(aegon_log PUBLIC
    Threads::Threads
)

# 1. Async io_uring Core (aegon_core)
add_library(aegon_core
    src/core/BufferPool.cpp
    src/core/IoUring.cpp
    src/core/EventLoop.cpp
)
target_link_libraries(aegon_core PUBLIC
    aegon_log
    uring
    Threads::Threads
)

# 2. Configuration System (aegon_config)
add_library(aegon_config
    src/config/DotEnv.cpp
    src/config/EnvExpander.cpp
)
target_link_libraries(aegon_config PUBLIC
    glaze::glaze
)

# 3. Redis Client (aegon_redis)
add_library(aegon_redis
    src/data/redis/Resp3.cpp
    src/data/redis/RedisConnection.cpp
    src/data/redis/RedisClient.cpp
    src/data/redis/RedisLock.cpp
)
target_link_libraries(aegon_redis PUBLIC
    aegon_core
    crypto
)

# 4. In-Memory Store (aegon_memory)
add_library(aegon_memory
    src/data/memory/MemStore.cpp
)
target_link_libraries(aegon_memory PUBLIC
    aegon_core
    uring
)

# 5. Unified ORM & SQL Query Engine (aegon_orm)
add_library(aegon_orm
    src/data/orm/sql/SqlParam.cpp
    src/data/orm/sql/Dialect.cpp
    src/data/orm/sql/Expression.cpp
    src/data/orm/sql/SelectBuilder.cpp
    src/data/orm/sql/OrmCache.cpp
    src/data/orm/sql/SchemaGenerator.cpp
    src/data/orm/sql/PerCoreConnectionPool.cpp
    src/data/orm/sql/drivers/SqliteDriver.cpp
    src/data/orm/sql/drivers/PostgresDriver.cpp
)
target_link_libraries(aegon_orm PUBLIC
    aegon_core
    glaze::glaze
    sqlite3
    pq
)

# 6. HTTP & WebSocket Server Engine (aegon_http)
add_library(aegon_http
    src/http/RadixTree.cpp
    src/http/Router.cpp
    src/http/Server.cpp
    src/http/v1/Http1Connection.cpp
    src/http/v1/Http1Serializer.cpp
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

# 7. Cloud-Native API Gateway & Proxy (aegon_gateway)
add_library(aegon_gateway
    src/gateway/UpstreamNode.cpp
    src/gateway/LoadBalancer.cpp
    src/gateway/CircuitBreaker.cpp
    src/gateway/UpstreamCluster.cpp
    src/gateway/ProxyHandler.cpp
)
target_link_libraries(aegon_gateway PUBLIC
    aegon_http
    aegon_core
)

# ==============================================================================
# Include Directories, Aliases & Export Names
# ==============================================================================
set(AEGON_MODULES log core config redis memory orm http gateway)

foreach(mod ${AEGON_MODULES})
    set_target_properties(aegon_${mod} PROPERTIES EXPORT_NAME ${mod})
    add_library(Aegon::${mod} ALIAS aegon_${mod})
endforeach()

foreach(tgt aegon_log aegon_core aegon_config aegon_redis aegon_memory aegon_orm aegon_http aegon_gateway)
    target_include_directories(${tgt} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/aegon>
    )
endforeach()

target_include_directories(aegon_orm PUBLIC
    $<$<BOOL:${PostgreSQL_FOUND}>:${PostgreSQL_INCLUDE_DIRS}>
)

# ==============================================================================
# Installation & Package Configuration
# ==============================================================================
install(
    TARGETS aegon_log aegon_core aegon_config aegon_redis aegon_memory aegon_orm aegon_http aegon_gateway
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
# Developer CLI Tool (aegon)
# ==============================================================================
add_executable(aegon_cli tools/cli/main.cpp)
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

# ==============================================================================
# Test Harness Helper & CTest Suite
# ==============================================================================
function(aegon_add_test name)
    add_executable(test_${name} tests/test_${name}.cpp)
    if(ARGN)
        target_link_libraries(test_${name} PRIVATE ${ARGN})
    else()
        target_link_libraries(test_${name} PRIVATE aegon_http aegon_core)
    endif()
    add_test(NAME ${name} COMMAND test_${name})
endfunction()

# Core & Utilities
add_executable(test_version tests/test_version.cpp)
target_include_directories(test_version PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME version COMMAND test_version)

aegon_add_test(simd_string aegon_core)
aegon_add_test(logger aegon_log)
aegon_add_test(foundational_types)
aegon_add_test(dto_validation)
aegon_add_test(advanced_io)
aegon_add_test(error_handling)

# HTTP & Web Protocols
aegon_add_test(radix_router)
aegon_add_test(simd_router)
aegon_add_test(middleware)
aegon_add_test(cors)
aegon_add_test(security_headers)
aegon_add_test(cookie)
aegon_add_test(jwt)
aegon_add_test(auth_middleware)
aegon_add_test(rbac_guard)
aegon_add_test(jwks)
aegon_add_test(compress aegon_http aegon_core ${LIBDEFLATE_LIB})
aegon_add_test(http_client)
aegon_add_test(service_registry)
aegon_add_test(websocket)
aegon_add_test(forms)
aegon_add_test(gateway aegon_gateway aegon_http aegon_core)
aegon_add_test(config aegon_config)

# Data, Redis & ORM
aegon_add_test(redis_protocol aegon_redis aegon_core)
aegon_add_test(redis_advanced aegon_redis aegon_core crypto)
aegon_add_test(live_redis_topologies aegon_redis aegon_core)
aegon_add_test(memstore aegon_memory aegon_core)
aegon_add_test(sql_schema aegon_orm aegon_http)
aegon_add_test(sql_query_builder aegon_orm aegon_http)
aegon_add_test(sql_transaction aegon_orm aegon_http)
aegon_add_test(sql_relations aegon_orm aegon_http)
aegon_add_test(sql_advanced_queries aegon_orm aegon_http)
aegon_add_test(sql_drivers aegon_orm aegon_http)
aegon_add_test(percore_clients aegon_orm aegon_http)
aegon_add_test(server_sql_config aegon_orm aegon_http)
aegon_add_test(orm_cache aegon_orm aegon_http)
aegon_add_test(orm_advanced aegon_orm aegon_http)
aegon_add_test(sql_tx_cache_parity aegon_orm aegon_http)
aegon_add_test(live_postgres aegon_orm aegon_http)
aegon_add_test(live_postgres_orm_cache aegon_orm aegon_redis aegon_http)

# CLI Integration Tests
add_test(NAME cli_version COMMAND aegon version)
add_test(NAME cli_help COMMAND aegon help)
add_test(NAME cli_doctor COMMAND aegon doctor)

# Standalone Test Servers & Harnesses (Non-CTest Executables)
add_executable(test_uuid tests/test_uuid.cpp)
target_link_libraries(test_uuid PRIVATE aegon_core)

add_executable(test_async_core tests/test_async_core.cpp)
target_link_libraries(test_async_core PRIVATE aegon_core)

add_executable(test_http_server tests/test_http_server.cpp)
target_link_libraries(test_http_server PRIVATE aegon_http aegon_core)

add_executable(test_http2_server tests/test_http2_server.cpp)
target_link_libraries(test_http2_server PRIVATE aegon_http aegon_core)

add_executable(test_http3_server tests/test_http3_server.cpp)
target_link_libraries(test_http3_server PRIVATE aegon_http aegon_core)

add_executable(test_http3_compliance tests/test_http3_compliance.cpp)
target_link_libraries(test_http3_compliance PRIVATE aegon_http aegon_core)

add_executable(test_tls_server tests/test_tls_server.cpp)
target_link_libraries(test_tls_server PRIVATE aegon_http aegon_core)

add_executable(test_spec_server tests/test_spec_server.cpp)
target_link_libraries(test_spec_server PRIVATE aegon_http aegon_core)

add_executable(autobahn_server tests/autobahn/server.cpp)
target_link_libraries(autobahn_server PRIVATE aegon_http aegon_core)

add_executable(test_sse tests/test_sse.cpp)
target_link_libraries(test_sse PRIVATE aegon_http aegon_core)
add_test(NAME sse COMMAND test_sse)

# ==============================================================================
# Samples
# ==============================================================================
add_subdirectory(samples/adv)
add_subdirectory(samples/sse)
