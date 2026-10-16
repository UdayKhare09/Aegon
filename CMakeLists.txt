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

# C++26 Standard (or /std:c++latest on MSVC)
if(MSVC)
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 26)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Optimization & Compiler Flags
if(MSVC)
    add_compile_options(
        /std:c++latest
        /utf-8
        /permissive-
        /EHsc
        /Zc:__cplusplus
        /W3
        /D_CRT_SECURE_NO_WARNINGS
        /DNOMINMAX
        /DWIN32_LEAN_AND_MEAN
        $<$<NOT:$<CONFIG:Debug>>:/O2>
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
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
find_package(glaze QUIET)
find_package(OpenSSL QUIET)
if(TARGET OpenSSL::Crypto)
    set(AEGON_CRYPTO_LIB OpenSSL::Crypto)
    set(AEGON_SSL_LIB OpenSSL::SSL)
else()
    set(AEGON_CRYPTO_LIB crypto)
    set(AEGON_SSL_LIB ssl)
endif()
find_package(PostgreSQL QUIET)
if(TARGET PostgreSQL::PostgreSQL)
    set(AEGON_POSTGRES_LIB PostgreSQL::PostgreSQL)
elseif(PostgreSQL_LIBRARIES)
    set(AEGON_POSTGRES_LIB ${PostgreSQL_LIBRARIES})
else()
    set(AEGON_POSTGRES_LIB pq)
endif()

find_package(SQLite3 QUIET)
if(TARGET SQLite3::SQLite3)
    set(AEGON_SQLITE_LIB SQLite3::SQLite3)
elseif(TARGET SQLite::SQLite3)
    set(AEGON_SQLITE_LIB SQLite::SQLite3)
else()
    find_package(unofficial-sqlite3 CONFIG QUIET)
    if(TARGET unofficial::sqlite3::sqlite3)
        set(AEGON_SQLITE_LIB unofficial::sqlite3::sqlite3)
    else()
        set(AEGON_SQLITE_LIB sqlite3)
    endif()
endif()

find_package(libdeflate CONFIG QUIET)
if(TARGET libdeflate::libdeflate_shared)
    set(AEGON_DEFLATE_LIB libdeflate::libdeflate_shared)
elseif(TARGET libdeflate::libdeflate_static)
    set(AEGON_DEFLATE_LIB libdeflate::libdeflate_static)
elseif(TARGET libdeflate::libdeflate)
    set(AEGON_DEFLATE_LIB libdeflate::libdeflate)
else()
    find_library(LIBDEFLATE_LIB NAMES deflate libdeflate QUIET)
    if(LIBDEFLATE_LIB)
        set(AEGON_DEFLATE_LIB ${LIBDEFLATE_LIB})
    else()
        set(AEGON_DEFLATE_LIB deflate)
    endif()
endif()

find_package(nghttp2 CONFIG QUIET)
if(TARGET nghttp2::nghttp2)
    set(AEGON_NGHTTP2_LIB nghttp2::nghttp2)
else()
    find_library(NGHTTP2_LIB NAMES nghttp2 QUIET)
    if(NGHTTP2_LIB)
        set(AEGON_NGHTTP2_LIB ${NGHTTP2_LIB})
    else()
        set(AEGON_NGHTTP2_LIB nghttp2)
    endif()
endif()

find_package(nghttp3 CONFIG QUIET)
if(TARGET nghttp3::nghttp3)
    set(AEGON_NGHTTP3_LIB nghttp3::nghttp3)
else()
    find_library(NGHTTP3_LIB NAMES nghttp3 QUIET)
    if(NGHTTP3_LIB)
        set(AEGON_NGHTTP3_LIB ${NGHTTP3_LIB})
    else()
        set(AEGON_NGHTTP3_LIB nghttp3)
    endif()
endif()

find_package(ngtcp2 CONFIG QUIET)
if(TARGET ngtcp2::ngtcp2_crypto_ossl)
    set(AEGON_NGTCP2_CRYPTO_LIB ngtcp2::ngtcp2_crypto_ossl)
else()
    find_library(NGTCP2_CRYPTO_LIB NAMES ngtcp2_crypto_ossl QUIET)
    if(NGTCP2_CRYPTO_LIB)
        set(AEGON_NGTCP2_CRYPTO_LIB ${NGTCP2_CRYPTO_LIB})
    else()
        set(AEGON_NGTCP2_CRYPTO_LIB ngtcp2_crypto_ossl)
    endif()
endif()

if(TARGET ngtcp2::ngtcp2)
    set(AEGON_NGTCP2_LIB ngtcp2::ngtcp2)
else()
    find_library(NGTCP2_LIB NAMES ngtcp2 QUIET)
    if(NGTCP2_LIB)
        set(AEGON_NGTCP2_LIB ${NGTCP2_LIB})
    else()
        set(AEGON_NGTCP2_LIB ngtcp2)
    endif()
endif()

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

# 1. Async I/O Engine Core (aegon_core)
set(AEGON_CORE_SOURCES
    src/core/BufferPool.cpp
    src/core/engine/IoEngine.cpp
    src/core/EventLoop.cpp
)

if(WIN32)
    list(APPEND AEGON_CORE_SOURCES
        src/core/engine/IocpEngine.cpp
    )
    add_library(aegon_core ${AEGON_CORE_SOURCES})
    target_link_libraries(aegon_core PUBLIC
        aegon_log
        ws2_32
        mswsock
        ntdll
        Threads::Threads
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND AEGON_CORE_SOURCES
        src/core/engine/IoUringEngine.cpp
        src/core/engine/EpollEngine.cpp
    )
    add_library(aegon_core ${AEGON_CORE_SOURCES})
    target_link_libraries(aegon_core PUBLIC
        aegon_log
        uring
        Threads::Threads
    )
else()
    add_library(aegon_core ${AEGON_CORE_SOURCES})
    target_link_libraries(aegon_core PUBLIC
        aegon_log
        Threads::Threads
    )
endif()

# 2. Configuration System (aegon_config)
if(TARGET glaze::glaze)
    add_library(aegon_config
        src/config/DotEnv.cpp
        src/config/EnvExpander.cpp
    )
    target_link_libraries(aegon_config PUBLIC
        glaze::glaze
    )
endif()

# 3. Redis Client (aegon_redis)
add_library(aegon_redis
    src/data/redis/Resp3.cpp
    src/data/redis/RedisConnection.cpp
    src/data/redis/RedisClient.cpp
    src/data/redis/RedisLock.cpp
)
target_link_libraries(aegon_redis PUBLIC
    aegon_core
    ${AEGON_CRYPTO_LIB}
)
if(WIN32)
    target_link_libraries(aegon_redis PUBLIC ws2_32)
endif()

# 4. In-Memory Store (aegon_memory)
if(NOT WIN32)
add_library(aegon_memory
    src/data/memory/MemStore.cpp
)
target_link_libraries(aegon_memory PUBLIC
    aegon_core
    uring
)
endif()

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
    ${AEGON_SQLITE_LIB}
    ${AEGON_POSTGRES_LIB}
)
if(PostgreSQL_INCLUDE_DIRS)
    target_include_directories(aegon_orm PUBLIC ${PostgreSQL_INCLUDE_DIRS})
endif()
if(SQLite3_INCLUDE_DIRS)
    target_include_directories(aegon_orm PUBLIC ${SQLite3_INCLUDE_DIRS})
endif()
if(WIN32)
    target_link_libraries(aegon_orm PUBLIC ws2_32)
endif()

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
    ${AEGON_DEFLATE_LIB}
    ${AEGON_NGHTTP2_LIB}
    ${AEGON_NGTCP2_CRYPTO_LIB}
    ${AEGON_NGTCP2_LIB}
    ${AEGON_NGHTTP3_LIB}
    ${AEGON_SSL_LIB}
    ${AEGON_CRYPTO_LIB}
)
if(WIN32)
    target_link_libraries(aegon_http PUBLIC ws2_32)
endif()

# ==============================================================================
# Include Directories, Aliases & Export Names
# ==============================================================================
set(AEGON_MODULES log core config redis memory orm http)

set(AEGON_ACTIVE_TARGETS "")
foreach(mod ${AEGON_MODULES})
    if(TARGET aegon_${mod})
        set_target_properties(aegon_${mod} PROPERTIES EXPORT_NAME ${mod})
        add_library(Aegon::${mod} ALIAS aegon_${mod})
        target_include_directories(aegon_${mod} PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/aegon>
        )
        list(APPEND AEGON_ACTIVE_TARGETS aegon_${mod})
    endif()
endforeach()

if(TARGET aegon_orm)
    target_include_directories(aegon_orm PUBLIC
        $<$<BOOL:${PostgreSQL_FOUND}>:${PostgreSQL_INCLUDE_DIRS}>
    )
endif()

# ==============================================================================
# Installation & Package Configuration
# ==============================================================================
if(AEGON_ACTIVE_TARGETS)
    install(
        TARGETS ${AEGON_ACTIVE_TARGETS}
        EXPORT AegonTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

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
if(NOT WIN32 AND TARGET aegon_core AND TARGET aegon_config)
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
endif()

# ==============================================================================
# Test Harness Helper & CTest Suite
# ==============================================================================
function(aegon_add_test name)
    if(ARGN)
        set(all_targets_exist TRUE)
        foreach(dep ${ARGN})
            if(dep MATCHES "^aegon_" AND NOT TARGET ${dep})
                set(all_targets_exist FALSE)
            endif()
        endforeach()
        if(all_targets_exist)
            add_executable(test_${name} tests/test_${name}.cpp)
            target_link_libraries(test_${name} PRIVATE ${ARGN})
            if(MSVC)
                target_link_options(test_${name} PRIVATE /STACK:8388608)
            endif()
            add_test(NAME ${name} COMMAND test_${name})
        endif()
    else()
        if(TARGET aegon_http AND TARGET aegon_core)
            add_executable(test_${name} tests/test_${name}.cpp)
            target_link_libraries(test_${name} PRIVATE aegon_http aegon_core)
            if(MSVC)
                target_link_options(test_${name} PRIVATE /STACK:8388608)
            endif()
            add_test(NAME ${name} COMMAND test_${name})
        endif()
    endif()
endfunction()

# Core & Utilities
add_executable(test_version tests/test_version.cpp)
target_include_directories(test_version PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME version COMMAND test_version)

aegon_add_test(simd_string aegon_core)
aegon_add_test(logger aegon_log)
if(TARGET glaze::glaze)
    aegon_add_test(foundational_types aegon_core glaze::glaze)
else()
    aegon_add_test(foundational_types aegon_core)
endif()
aegon_add_test(dto_validation)
if(WIN32)
    aegon_add_test(iocp_engine aegon_core ws2_32 mswsock ntdll)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    aegon_add_test(advanced_io)
    aegon_add_test(epoll_engine aegon_core aegon_http)
endif()
aegon_add_test(error_handling)

# HTTP & Web Protocols
aegon_add_test(radix_router)
aegon_add_test(simd_router)
aegon_add_test(middleware)
aegon_add_test(access_log)
aegon_add_test(cors)
aegon_add_test(security_headers)
aegon_add_test(cookie)
aegon_add_test(jwt)
aegon_add_test(auth_middleware)
aegon_add_test(rbac_guard)
aegon_add_test(jwks)
aegon_add_test(compress aegon_http aegon_core ${AEGON_DEFLATE_LIB})
aegon_add_test(http_client)
aegon_add_test(service_registry)
aegon_add_test(websocket)
aegon_add_test(forms)
aegon_add_test(config aegon_config)

# Data, Redis & ORM
aegon_add_test(redis_protocol aegon_redis aegon_core)
aegon_add_test(redis_advanced aegon_redis aegon_core ${AEGON_CRYPTO_LIB})
aegon_add_test(live_redis_topologies aegon_redis aegon_core)
aegon_add_test(memstore aegon_memory aegon_core)
aegon_add_test(sql_schema aegon_orm)
aegon_add_test(sql_query_builder aegon_orm)
aegon_add_test(sql_transaction aegon_orm aegon_http)
aegon_add_test(sql_relations aegon_orm)
aegon_add_test(sql_advanced_queries aegon_orm)
aegon_add_test(sql_drivers aegon_orm)
aegon_add_test(percore_clients aegon_orm aegon_http)
aegon_add_test(server_sql_config aegon_orm aegon_http)
aegon_add_test(orm_cache aegon_orm)
aegon_add_test(orm_advanced aegon_orm)
aegon_add_test(sql_tx_cache_parity aegon_orm)
aegon_add_test(live_postgres aegon_orm aegon_http)
aegon_add_test(live_postgres_orm_cache aegon_orm aegon_redis)

# CLI Integration Tests
if(TARGET aegon_cli)
    add_test(NAME cli_version COMMAND aegon version)
    add_test(NAME cli_help COMMAND aegon help)
    add_test(NAME cli_doctor COMMAND aegon doctor)
endif()

# Standalone Test Servers & Harnesses (Non-CTest Executables)
add_executable(test_uuid tests/test_uuid.cpp)
target_link_libraries(test_uuid PRIVATE aegon_core)

if(NOT WIN32)
    add_executable(test_async_core tests/test_async_core.cpp)
    target_link_libraries(test_async_core PRIVATE aegon_core)
endif()

if(TARGET aegon_http)
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
endif()

# ==============================================================================
# Samples
# ==============================================================================
if(TARGET aegon_http AND TARGET aegon_orm AND TARGET aegon_redis)
    add_subdirectory(samples/adv)
    add_subdirectory(samples/sse)
endif()
