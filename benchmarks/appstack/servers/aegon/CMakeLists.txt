cmake_minimum_required(VERSION 3.25)
project(bench_aegon_appstack LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3 -march=native -Wall -Wextra)
endif()

set(CMAKE_PREFIX_PATH "/tmp/aegon_install" ${CMAKE_PREFIX_PATH})
find_package(Aegon REQUIRED)

add_executable(bench_aegon main.cpp)
target_link_libraries(bench_aegon PRIVATE
    Aegon::http
    Aegon::core
    Aegon::orm
    Aegon::redis
)
