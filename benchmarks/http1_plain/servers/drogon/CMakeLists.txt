cmake_minimum_required(VERSION 3.25)
project(bench_drogon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3 -march=native -Wall -Wextra)
endif()

set(CMAKE_PREFIX_PATH "/tmp/drogon_install" ${CMAKE_PREFIX_PATH})
find_package(Drogon REQUIRED)

add_executable(bench_drogon main.cpp)
target_link_libraries(bench_drogon PRIVATE Drogon::Drogon)
