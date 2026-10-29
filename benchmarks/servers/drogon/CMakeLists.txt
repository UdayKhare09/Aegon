cmake_minimum_required(VERSION 3.20)
project(bench_drogon CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local")

find_package(Drogon CONFIG REQUIRED)

add_executable(bench_drogon_bin main.cc)

target_compile_options(bench_drogon_bin PRIVATE -O3 -march=native -flto -DNDEBUG)
target_link_options(bench_drogon_bin PRIVATE -O3 -march=native -flto)

target_link_libraries(bench_drogon_bin PRIVATE Drogon::Drogon)

