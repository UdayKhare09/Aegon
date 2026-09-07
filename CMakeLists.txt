cmake_minimum_required(VERSION 3.20)
project(Aegon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable aggressive optimization and Zen 4 / modern x86 instruction sets
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
    aegon_uuid
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
