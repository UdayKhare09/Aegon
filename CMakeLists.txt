cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Aegon Web Framework & High-Performance C++23 Engine
# ==============================================================================

set(AEGON_VERSION_MAJOR 0)
set(AEGON_VERSION_MINOR 2)
set(AEGON_VERSION_PATCH 1)
set(AEGON_VERSION_STRING "${AEGON_VERSION_MAJOR}.${AEGON_VERSION_MINOR}.${AEGON_VERSION_PATCH}")

# Disallow in-source builds to prevent root directory pollution
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are forbidden.\n"
        "Please use an out-of-source build directory (e.g. 'cmake -B build')."
    )
endif()

project(Aegon
    VERSION ${AEGON_VERSION_MAJOR}.${AEGON_VERSION_MINOR}.${AEGON_VERSION_PATCH}
    LANGUAGES C CXX
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "Aegon is an ultra-high-performance Linux-first asynchronous framework.\n"
        "It requires Linux (kernel 5.10+ recommended with io_uring and epoll support).\n"
        "Windows and macOS are currently not supported."
    )
endif()

include(GNUInstallDirs)

# User-configurable Build Options
option(AEGON_BUILD_TESTS "Build Aegon test suite and benchmarks harness" ON)
option(AEGON_BUILD_BENCHMARKS "Build Aegon benchmark servers" OFF)
option(AEGON_BUILD_C_ABI "Build the C ABI wrapper library (libaegon_c)" ON)
option(AEGON_BUILD_C_EXAMPLES "Build C ABI examples" OFF)

if(AEGON_BUILD_TESTS)
    enable_testing()
endif()

# ==============================================================================
# Compiler Options & Third-Party Dependencies
# ==============================================================================
include(cmake/CompilerOptions.cmake)
include(cmake/Hardening.cmake)
include(cmake/Dependencies.cmake)

# ==============================================================================
# Core Framework Modules
# ==============================================================================
add_subdirectory(src)

# ==============================================================================
# Installation & Package Configuration
# ==============================================================================
include(cmake/InstallAndExport.cmake)

# ==============================================================================
# Test Suite & CTest Registrations
# ==============================================================================
if(AEGON_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ==============================================================================
# Benchmarks
# ==============================================================================
if(AEGON_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# ==============================================================================
# C ABI Examples
# ==============================================================================
if(AEGON_BUILD_C_ABI AND AEGON_BUILD_C_EXAMPLES)
    add_subdirectory(examples/c)
endif()

# ==============================================================================
# CPack Packaging Configuration
# ==============================================================================
set(CPACK_PACKAGE_NAME              "aegon")
set(CPACK_PACKAGE_VENDOR            "Uday Khare")
set(CPACK_PACKAGE_CONTACT           "udaykhare77@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Ultra-High-Performance C++23 Linux Asynchronous Web & Data Framework")
set(CPACK_PACKAGE_HOMEPAGE_URL      "https://github.com/UdayKhare09/Aegon")
set(CPACK_PACKAGE_VERSION_MAJOR     ${AEGON_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR     ${AEGON_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH     ${AEGON_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION           "${AEGON_VERSION_STRING}")
set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# Binary packages: compressed tarballs (TGZ, TXZ), Debian (.deb), RPM (.rpm)
set(CPACK_GENERATOR                 "TGZ;TXZ;DEB;RPM")

# Debian Package (.deb)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_SECTION    "devel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY   "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS  ON)

# RPM Package (.rpm)
set(CPACK_RPM_PACKAGE_LICENSE       "MIT")
set(CPACK_RPM_PACKAGE_GROUP         "Development/Libraries")
set(CPACK_RPM_PACKAGE_AUTOREQ       ON)

# Source distribution tarballs
set(CPACK_SOURCE_GENERATOR          "TGZ;TXZ")
set(CPACK_SOURCE_IGNORE_FILES       "/\\.git/;/build/;/\\.cache/;/build-.*/")

include(CPack)
