# ==============================================================================
# Aegon Unified Benchmark Server (HttpArena Spec Parity)
# ==============================================================================

if(TARGET aegon_http)
    add_executable(bench_aegon servers/aegon/main.cpp)
    target_include_directories(bench_aegon PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(bench_aegon PRIVATE aegon_http aegon_core aegon_memory)

    add_custom_command(TARGET bench_aegon POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:bench_aegon>
            "${CMAKE_CURRENT_SOURCE_DIR}/servers/aegon/bench_aegon"
        COMMENT "Copying bench_aegon to benchmarks/servers/aegon/bench_aegon"
    )
endif()
