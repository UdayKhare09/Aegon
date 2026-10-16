# ==============================================================================
# Aegon Compiler Options, Optimization & Language Standards (C++23)
# ==============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(MSVC)
    add_compile_options(
        /std:c++latest
        /utf-8
        /permissive-
        /EHsc
        /Zc:__cplusplus
        /W3
        /D_CRT_SECURE_NO_WARNINGS
        /DNOMINMAX
        /DWIN32_LEAN_AND_MEAN
        $<$<NOT:$<CONFIG:Debug>>:/O2>
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -O3
        -march=native
        -Wall
        -Wextra
        -Wpedantic
        -Wno-missing-field-initializers
        -Wno-missing-braces
    )
endif()
