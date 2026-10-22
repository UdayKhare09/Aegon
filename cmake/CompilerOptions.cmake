# ==============================================================================
# Aegon Compiler Options, Optimization & Language Standards (C++23)
# ==============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Fast compiler caching (ccache)
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "[Build Speed] ccache detected and enabled: ${CCACHE_PROGRAM}")
endif()

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

    # Ultra-fast modern linker (mold / lld)
    find_program(MOLD_LINKER mold)
    if(MOLD_LINKER)
        add_link_options("-fuse-ld=mold")
        message(STATUS "[Build Speed] Ultra-fast linker detected: mold (${MOLD_LINKER})")
    else()
        find_program(LLD_LINKER lld)
        if(LLD_LINKER)
            add_link_options("-fuse-ld=lld")
            message(STATUS "[Build Speed] Fast linker detected: lld (${LLD_LINKER})")
        endif()
    endif()
endif()
