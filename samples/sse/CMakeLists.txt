cmake_minimum_required(VERSION 3.28)

add_executable(aegon_sse_sample
    src/main.cpp
)

target_include_directories(aegon_sse_sample PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(aegon_sse_sample PRIVATE
    Aegon::http
    Aegon::core
)
