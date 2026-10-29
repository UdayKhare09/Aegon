cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Aegon Web Framework & High-Performance C++23 Engine
# ==============================================================================

set(AEGON_VERSION_MAJOR 0)
set(AEGON_VERSION_MINOR 2)
set(AEGON_VERSION_PATCH 1)
set(AEGON_VERSION_STRING "${AEGON_VERSION_MAJOR}.${AEGON_VERSION_MINOR}.${AEGON_VERSION_PATCH}")

# Disallow in-source builds to prevent root directory pollution
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are forbidden.\n"
        "Please use an out-of-source build directory (e.g. 'cmake -B build' or 'cmake -B build-asan')."
    )
endif()

project(Aegon
    VERSION ${AEGON_VERSION_MAJOR}.${AEGON_VERSION_MINOR}.${AEGON_VERSION_PATCH}
    LANGUAGES C CXX
)

include(GNUInstallDirs)

# User-configurable Build Options
option(AEGON_BUILD_TESTS "Build Aegon test suite and benchmarks harness" ON)
option(AEGON_BUILD_BENCHMARKS "Build Aegon benchmark servers" OFF)

if(AEGON_BUILD_TESTS)
    enable_testing()
endif()

# ==============================================================================
# Compiler Options & Third-Party Dependencies
# ==============================================================================
include(cmake/CompilerOptions.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Hardening.cmake)
include(cmake/Dependencies.cmake)
include(cmake/ClangTidy.cmake)
include(cmake/Fuzzing.cmake)

# ==============================================================================
# Core Framework Libraries
# ==============================================================================

# 0. High-Performance Telemetry & Logging (aegon_log)
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

target_precompile_headers(aegon_core PRIVATE
    <vector>
    <string>
    <string_view>
    <span>
    <memory>
    <optional>
    <chrono>
    <coroutine>
)

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
add_library(aegon_memory
    src/data/memory/MemStore.cpp
)
target_link_libraries(aegon_memory PUBLIC
    aegon_core
)
if(NOT WIN32 AND TARGET uring)
    target_link_libraries(aegon_memory PUBLIC uring)
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
    ${AEGON_PROTOBUF_LIB}
)
if(Protobuf_INCLUDE_DIRS)
    target_include_directories(aegon_http PUBLIC ${Protobuf_INCLUDE_DIRS})
endif()
if(WIN32)
    target_link_libraries(aegon_http PUBLIC ws2_32)
endif()

target_precompile_headers(aegon_http PRIVATE
    <vector>
    <string>
    <string_view>
    <span>
    <memory>
    <optional>
    <chrono>
    <array>
    <unordered_map>
    <functional>
    <glaze/glaze.hpp>
    <openssl/ssl.h>
    <openssl/crypto.h>
)

# 7. C ABI Wrapper Library (libaegon_c shared + static)
option(AEGON_BUILD_C_ABI "Build the C ABI wrapper library (libaegon_c)" ON)

if(AEGON_BUILD_C_ABI)
    set(AEGON_C_SOURCES
        src/c/error.cpp
        src/c/log.cpp
        src/c/request.cpp
        src/c/response.cpp
        src/c/context.cpp
        src/c/router.cpp
        src/c/server.cpp
        src/c/middleware.cpp
        src/c/cors.cpp
        src/c/compress.cpp
        src/c/security.cpp
        src/c/static_files.cpp
        src/c/multipart.cpp
        src/c/async.cpp
        src/c/websocket.cpp
        src/c/sse.cpp
    )

    # Static archive: libaegon_c.a (or aegon_c_static.lib on Windows)
    add_library(aegon_c_static STATIC ${AEGON_C_SOURCES})
    add_library(Aegon::c_static ALIAS aegon_c_static)
    target_link_libraries(aegon_c_static PRIVATE aegon_http)
    target_compile_definitions(aegon_c_static PRIVATE AEGON_C_BUILDING)
    target_compile_definitions(aegon_c_static PUBLIC AEGON_C_STATIC)
    set_target_properties(aegon_c_static PROPERTIES
        OUTPUT_NAME aegon_c
        EXPORT_NAME c_static
        POSITION_INDEPENDENT_CODE ON
    )
    if(WIN32)
        set_target_properties(aegon_c_static PROPERTIES OUTPUT_NAME aegon_c_static)
    endif()
    target_include_directories(aegon_c_static PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_include_directories(aegon_c_static PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    # Shared library: libaegon_c.so / aegon_c.dll (for FFIs)
    add_library(aegon_c_shared SHARED ${AEGON_C_SOURCES})
    add_library(Aegon::c_shared ALIAS aegon_c_shared)
    add_library(Aegon::c ALIAS aegon_c_shared)
    add_library(aegon_c ALIAS aegon_c_shared)
    target_link_libraries(aegon_c_shared PRIVATE aegon_http)
    target_compile_definitions(aegon_c_shared PRIVATE AEGON_C_BUILDING)
    set_target_properties(aegon_c_shared PROPERTIES
        OUTPUT_NAME aegon_c
        EXPORT_NAME c
        VERSION   ${AEGON_VERSION_STRING}
        SOVERSION 1
        C_VISIBILITY_PRESET     hidden
        CXX_VISIBILITY_PRESET   hidden
        VISIBILITY_INLINES_HIDDEN ON
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(aegon_c_shared PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_include_directories(aegon_c_shared PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(UNIX AND NOT APPLE)
        target_link_options(aegon_c_shared PRIVATE
            "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/aegon_c.map")
    endif()

    include(cmake/CheckCHeaders.cmake)
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

if(AEGON_BUILD_C_ABI AND TARGET aegon_c_shared)
    list(APPEND AEGON_ACTIVE_TARGETS aegon_c_shared aegon_c_static)
endif()

if(TARGET aegon_orm)
    target_include_directories(aegon_orm PUBLIC
        $<$<BOOL:${PostgreSQL_FOUND}>:${PostgreSQL_INCLUDE_DIRS}>
    )
endif()

# ==============================================================================
# Installation & Package Configuration
# ==============================================================================
include(cmake/InstallAndExport.cmake)


# ==============================================================================
# Test Suite & CTest Registrations
# ==============================================================================
if(AEGON_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ==============================================================================
# Benchmarks
# ==============================================================================
if(AEGON_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# ==============================================================================
# C ABI Examples
# ==============================================================================
option(AEGON_BUILD_C_EXAMPLES "Build C ABI examples" OFF)
if(AEGON_BUILD_C_ABI AND AEGON_BUILD_C_EXAMPLES)
    add_subdirectory(examples/c)
endif()


# ==============================================================================
# CPack Packaging
# ==============================================================================
set(CPACK_PACKAGE_NAME              "aegon")
set(CPACK_PACKAGE_VENDOR            "Uday Khare")
set(CPACK_PACKAGE_CONTACT           "udaykhare77@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-Performance C++23 Asynchronous Web & Data Framework")
set(CPACK_PACKAGE_VERSION_MAJOR     ${AEGON_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR     ${AEGON_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH     ${AEGON_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION           "${AEGON_VERSION_STRING}")
set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# Source tarball
set(CPACK_SOURCE_GENERATOR          "TGZ")
set(CPACK_SOURCE_IGNORE_FILES       "/\\.git/;/build/;/\\.cache/")

include(CPack)
