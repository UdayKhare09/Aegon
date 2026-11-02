# ==============================================================================
# Aegon Compiler Options, Optimization & Language Standards (C++23)
# ==============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release build if no build type is specified (for single-configuration generators)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose build type: Debug Release RelWithDebInfo MinSizeRel" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

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

# Optional native CPU host tuning (-march=native)
option(AEGON_ENABLE_NATIVE_TUNING "Optimize binaries for host CPU (-march=native)" OFF)

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wno-missing-field-initializers
    -Wno-missing-braces
)

if(AEGON_ENABLE_NATIVE_TUNING)
    add_compile_options(-march=native)
    message(STATUS "[Compiler Options] Host CPU native tuning enabled (-march=native)")
endif()

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
