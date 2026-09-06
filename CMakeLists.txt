cmake_minimum_required(VERSION 3.20)
project(Aegon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
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

# Core library
add_library(aegon_uuid
    src/data/uuid/UUID.cpp
    src/data/uuid/UUIDGenerator.cpp
)

target_include_directories(aegon_uuid PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Test and Benchmark executable
add_executable(test_uuid
    tests/test_uuid.cpp
)

target_link_libraries(test_uuid PRIVATE aegon_uuid)
