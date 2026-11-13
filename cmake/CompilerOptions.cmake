# ==============================================================================
# Aegon Compiler Options, Optimization & Language Standards (C++23)
# ==============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Minimum compiler version validation for complete C++23 feature support
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
        message(FATAL_ERROR "Aegon requires GCC 13.0 or newer for C++23 language support. Detected: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "17.0")
        message(FATAL_ERROR "Aegon requires Clang 17.0 or newer for C++23 language support. Detected: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
endif()

# Fast compiler caching (ccache for both C and C++)
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "[Build Speed] ccache detected and enabled: ${CCACHE_PROGRAM}")
endif()

# Aegon always builds optimized for the host CPU. There are no build profiles.
add_compile_options(
    -O3
    -march=native
)

add_compile_options(
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
