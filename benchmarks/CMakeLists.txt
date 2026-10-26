# ==============================================================================
# Aegon Unified Benchmark Server
# ==============================================================================

if(TARGET aegon_http AND TARGET aegon_orm)
    add_executable(bench_aegon http3/servers/aegon/main.cpp)
    target_link_libraries(bench_aegon PRIVATE aegon_http aegon_core aegon_orm)
    target_include_directories(bench_aegon PRIVATE ${CMAKE_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(bench_aegon PRIVATE /std:c++latest /EHsc)
    endif()
endif()

if(TARGET aegon_http)
    add_executable(bench_grpc grpc/main.cpp ${CMAKE_SOURCE_DIR}/tests/proto/benchmark.pb.cc)
    target_include_directories(bench_grpc PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/tests/proto)
    target_link_libraries(bench_grpc PRIVATE aegon_http aegon_core ${AEGON_PROTOBUF_LIB})
endif()
