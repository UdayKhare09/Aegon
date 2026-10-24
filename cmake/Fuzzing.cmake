# ==============================================================================
# Aegon Security: LibFuzzer Coverage-Guided Fuzz Testing
# ==============================================================================

option(AEGON_BUILD_FUZZERS "Build LibFuzzer targets for untrusted input parsers" OFF)

if(AEGON_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "LibFuzzer requires Clang (clang++). Current compiler: ${CMAKE_CXX_COMPILER_ID}")
    endif()

    message(STATUS "[Fuzzing] LibFuzzer targets enabled")

    # Enable coverage-guided fuzzer instrumentation with ASan + UBSan
    add_compile_options(
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
        -g
    )
    add_link_options(
        -fsanitize=fuzzer,address,undefined
    )

    add_subdirectory(tests/fuzz)
endif()
