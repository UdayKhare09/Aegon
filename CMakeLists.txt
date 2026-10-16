cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Aegon Web Framework & High-Performance C++23 Engine
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

include(GNUInstallDirs)

# User-configurable Build Options
option(AEGON_BUILD_TESTS "Build Aegon test suite and benchmarks harness" ON)
option(AEGON_BUILD_CLI "Build Aegon developer CLI tool" ON)
option(AEGON_BUILD_SAMPLES "Build Aegon sample applications" ON)
option(AEGON_BUILD_BENCHMARKS "Build Aegon unified benchmark server" ON)

if(AEGON_BUILD_TESTS)
    enable_testing()
endif()

# ==============================================================================
# Compiler Options & Third-Party Dependencies
# ==============================================================================
include(cmake/CompilerOptions.cmake)
include(cmake/Dependencies.cmake)

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
include(cmake/InstallAndExport.cmake)

# ==============================================================================
# Developer CLI Tool (aegon)
# ==============================================================================
if(AEGON_BUILD_CLI)
    add_subdirectory(tools/cli)
endif()

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
# Samples
# ==============================================================================
if(AEGON_BUILD_SAMPLES AND TARGET aegon_http AND TARGET aegon_orm AND TARGET aegon_redis)
    add_subdirectory(samples/adv)
    add_subdirectory(samples/sse)
endif()
