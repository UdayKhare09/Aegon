# ==============================================================================
# Aegon Clang-Tidy Static Analysis Integration
# ==============================================================================

option(AEGON_ENABLE_CLANG_TIDY "Run clang-tidy automatically on compiler invocations" OFF)

find_program(CLANG_TIDY_BIN NAMES clang-tidy)
find_program(RUN_CLANG_TIDY_BIN NAMES run-clang-tidy run-clang-tidy.py)

if(CLANG_TIDY_BIN)
    message(STATUS "[Static Analysis] clang-tidy detected: ${CLANG_TIDY_BIN}")

    if(AEGON_ENABLE_CLANG_TIDY)
        message(STATUS "[Static Analysis] Compiler-level clang-tidy enabled")
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_BIN}")
    endif()

    if(RUN_CLANG_TIDY_BIN)
        message(STATUS "[Static Analysis] run-clang-tidy detected: ${RUN_CLANG_TIDY_BIN}")
        add_custom_target(clang-tidy
            COMMAND ${RUN_CLANG_TIDY_BIN}
                -p "${CMAKE_BINARY_DIR}"
                -j 0
                -source-filter ".*/src/.*"
                -quiet
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Executing parallel clang-tidy analysis across src/..."
            USES_TERMINAL
        )
    else()
        add_custom_target(clang-tidy
            COMMAND ${CLANG_TIDY_BIN}
                -p "${CMAKE_BINARY_DIR}"
                --quiet
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Executing clang-tidy analysis..."
            USES_TERMINAL
        )
    endif()
else()
    message(STATUS "[Static Analysis] clang-tidy not found")
endif()
