# ==============================================================================
# Aegon Third-Party Dependencies Resolution & Imported Targets
# Standardized namespace: Aegon::Ext::<Name>
#
# Every required system dependency is resolved at configure time. If a header
# or library is missing, configuration stops immediately with an install hint
# instead of falling back to a bare linker name and failing later at link time.
# ==============================================================================

find_package(PkgConfig QUIET)

# ------------------------------------------------------------------------------
# Helpers
# ------------------------------------------------------------------------------

# aegon_find_required_library(
#     TARGET <Aegon::Ext::name>
#     DISPLAY <human-readable name>
#     HEADER <header to find>
#     LIBRARIES <library name>...
#     [PKG_CONFIG <pkg-config module>]
#     [ROOTS <root variable>...]
#     [ARCH <Arch package>]
#     [DEBIAN <Debian package>]
# )
#
# Resolve a dependency that may not ship a CMake config package. pkg-config is
# tried first, then a manual header/library search using the supplied root
# variables (both CMake cache variables and environment variables).
function(aegon_find_required_library)
    cmake_parse_arguments(ARG ""
        "TARGET;DISPLAY;HEADER;PKG_CONFIG;ARCH;DEBIAN"
        "LIBRARIES;ROOTS"
        ${ARGN})

    if(TARGET "${ARG_TARGET}")
        return()
    endif()

    if(NOT ARG_TARGET OR NOT ARG_DISPLAY OR NOT ARG_HEADER OR NOT ARG_LIBRARIES)
        message(FATAL_ERROR
            "aegon_find_required_library: TARGET, DISPLAY, HEADER and LIBRARIES are required")
    endif()

    string(MAKE_C_IDENTIFIER "${ARG_TARGET}" _dep_id)
    set(_pkg_prefix "AEGON_PKG_${_dep_id}")
    set(_pkg_target "PkgConfig::${_pkg_prefix}")

    # Prefer pkg-config because it carries transitive link dependencies and
    # private flags that a bare find_library() cannot know about.
    if(ARG_PKG_CONFIG AND PKG_CONFIG_FOUND)
        pkg_check_modules(${_pkg_prefix} QUIET IMPORTED_TARGET "${ARG_PKG_CONFIG}")
        if(TARGET "${_pkg_target}")
            add_library(${ARG_TARGET} INTERFACE IMPORTED GLOBAL)
            target_link_libraries(${ARG_TARGET} INTERFACE ${_pkg_target})
            message(STATUS
                "[Dependency] ${ARG_DISPLAY} found via pkg-config (${${_pkg_prefix}_VERSION})")
            return()
        endif()
    endif()

    set(_include_hints "")
    set(_library_hints "")
    foreach(_root_var IN LISTS ARG_ROOTS)
        foreach(_root IN ITEMS "${${_root_var}}" "$ENV{${_root_var}}")
            if(_root)
                list(APPEND _include_hints "${_root}" "${_root}/include")
                list(APPEND _library_hints "${_root}" "${_root}/lib" "${_root}/lib64")
            endif()
        endforeach()
    endforeach()

    find_path(AEGON_${_dep_id}_INCLUDE_DIR
        NAMES ${ARG_HEADER}
        HINTS ${_include_hints}
    )
    find_library(AEGON_${_dep_id}_LIBRARY
        NAMES ${ARG_LIBRARIES}
        HINTS ${_library_hints}
    )

    if(NOT AEGON_${_dep_id}_INCLUDE_DIR OR NOT AEGON_${_dep_id}_LIBRARY)
        set(_install_hints "")
        if(ARG_ARCH)
            string(APPEND _install_hints "  Arch:   sudo pacman -S ${ARG_ARCH}\n")
        endif()
        if(ARG_DEBIAN)
            string(APPEND _install_hints "  Debian: sudo apt install ${ARG_DEBIAN}\n")
        endif()

        if(ARG_ROOTS)
            list(GET ARG_ROOTS 0 _primary_root)
            set(_root_hint "Or set ${_primary_root} to the install prefix.")
        else()
            set(_root_hint "Install the development package and re-run CMake.")
        endif()

        if(_install_hints)
            set(_install_hints "Install it with:\n${_install_hints}")
        endif()

        message(FATAL_ERROR
            "Missing required dependency: ${ARG_DISPLAY}\n"
            "${_install_hints}"
            "${_root_hint}"
        )
    endif()

    add_library(${ARG_TARGET} UNKNOWN IMPORTED GLOBAL)
    set_target_properties(${ARG_TARGET} PROPERTIES
        IMPORTED_LOCATION "${AEGON_${_dep_id}_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${AEGON_${_dep_id}_INCLUDE_DIR}"
    )
    message(STATUS "[Dependency] ${ARG_DISPLAY} found: ${AEGON_${_dep_id}_LIBRARY}")
endfunction()

# ------------------------------------------------------------------------------
# 1. Threads
# ------------------------------------------------------------------------------
find_package(Threads REQUIRED)
if(NOT TARGET Aegon::Ext::Threads)
    add_library(Aegon::Ext::Threads INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::Threads INTERFACE Threads::Threads)
endif()

# ------------------------------------------------------------------------------
# 2. Linux io_uring (liburing)
# ------------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    aegon_find_required_library(
        TARGET      Aegon::Ext::uring
        DISPLAY     "liburing"
        HEADER      liburing.h
        LIBRARIES   uring
        PKG_CONFIG  liburing
        ROOTS       URING_ROOT LIBURING_ROOT
        ARCH        liburing
        DEBIAN      liburing-dev
    )
endif()

# ------------------------------------------------------------------------------
# 3. Serialization: Glaze (header-only)
# ------------------------------------------------------------------------------
find_package(glaze CONFIG QUIET)
if(NOT TARGET glaze::glaze)
    include(FetchContent)
    message(STATUS "[Dependency] glaze not found on system; acquiring via FetchContent (v9.1.0)...")
    FetchContent_Declare(
        glaze
        GIT_REPOSITORY https://github.com/stephenberry/glaze.git
        GIT_TAG        v9.1.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glaze)
endif()

if(NOT TARGET glaze::glaze)
    message(FATAL_ERROR
        "Missing required dependency: glaze\n"
        "  Arch:   sudo pacman -S glaze\n"
        "  Others: build from source or install via vcpkg\n"
        "Or set CMAKE_PREFIX_PATH to the glaze install prefix."
    )
endif()

if(NOT TARGET Aegon::Ext::glaze)
    add_library(Aegon::Ext::glaze INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::glaze INTERFACE glaze::glaze)
endif()

# ------------------------------------------------------------------------------
# 4. Cryptography & TLS: OpenSSL
# ------------------------------------------------------------------------------
find_package(OpenSSL 3.0 QUIET)
# FindOpenSSL sets OPENSSL_FOUND; newer CMake versions also set OpenSSL_FOUND.
if((NOT OPENSSL_FOUND AND NOT OpenSSL_FOUND) OR NOT TARGET OpenSSL::SSL OR NOT TARGET OpenSSL::Crypto)
    message(FATAL_ERROR
        "Missing required dependency: OpenSSL >= 3.0\n"
        "  Arch:   sudo pacman -S openssl\n"
        "  Debian: sudo apt install libssl-dev\n"
        "Or set OPENSSL_ROOT_DIR to the install prefix."
    )
endif()

if(NOT TARGET Aegon::Ext::OpenSSL_Crypto)
    add_library(Aegon::Ext::OpenSSL_Crypto INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::OpenSSL_Crypto INTERFACE OpenSSL::Crypto)
endif()
if(NOT TARGET Aegon::Ext::OpenSSL_SSL)
    add_library(Aegon::Ext::OpenSSL_SSL INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::OpenSSL_SSL INTERFACE OpenSSL::SSL)
endif()

# ------------------------------------------------------------------------------
# 5. Database: PostgreSQL (libpq)
# ------------------------------------------------------------------------------
find_package(PostgreSQL QUIET)
if(TARGET PostgreSQL::PostgreSQL)
    add_library(Aegon::Ext::PostgreSQL INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::PostgreSQL INTERFACE PostgreSQL::PostgreSQL)
    message(STATUS "[Dependency] PostgreSQL found via CMake package")
else()
    aegon_find_required_library(
        TARGET      Aegon::Ext::PostgreSQL
        DISPLAY     "PostgreSQL (libpq)"
        HEADER      libpq-fe.h
        LIBRARIES   pq
        PKG_CONFIG  libpq
        ROOTS       PostgreSQL_ROOT PQ_ROOT
        ARCH        postgresql-libs
        DEBIAN      libpq-dev
    )
endif()

# ------------------------------------------------------------------------------
# 6. Database: SQLite3
# ------------------------------------------------------------------------------
find_package(SQLite3 QUIET)
if(TARGET SQLite3::SQLite3)
    add_library(Aegon::Ext::SQLite3 INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::SQLite3 INTERFACE SQLite3::SQLite3)
    message(STATUS "[Dependency] SQLite3 found via CMake package")
elseif(TARGET SQLite::SQLite3)
    add_library(Aegon::Ext::SQLite3 INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::SQLite3 INTERFACE SQLite::SQLite3)
    message(STATUS "[Dependency] SQLite3 found via CMake package")
elseif(TARGET unofficial::sqlite3::sqlite3)
    add_library(Aegon::Ext::SQLite3 INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::SQLite3 INTERFACE unofficial::sqlite3::sqlite3)
    message(STATUS "[Dependency] SQLite3 found via CMake package")
else()
    aegon_find_required_library(
        TARGET      Aegon::Ext::SQLite3
        DISPLAY     "SQLite3"
        HEADER      sqlite3.h
        LIBRARIES   sqlite3
        PKG_CONFIG  sqlite3
        ROOTS       SQLite3_ROOT SQLITE3_ROOT
        ARCH        sqlite
        DEBIAN      libsqlite3-dev
    )
endif()

# ------------------------------------------------------------------------------
# 7. Compression: libdeflate
# ------------------------------------------------------------------------------
find_package(libdeflate CONFIG QUIET)
if(TARGET libdeflate::libdeflate_shared)
    add_library(Aegon::Ext::deflate INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::deflate INTERFACE libdeflate::libdeflate_shared)
    message(STATUS "[Dependency] libdeflate found via CMake package (shared)")
elseif(TARGET libdeflate::libdeflate_static)
    add_library(Aegon::Ext::deflate INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::deflate INTERFACE libdeflate::libdeflate_static)
    message(STATUS "[Dependency] libdeflate found via CMake package (static)")
elseif(TARGET libdeflate::libdeflate)
    add_library(Aegon::Ext::deflate INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::deflate INTERFACE libdeflate::libdeflate)
    message(STATUS "[Dependency] libdeflate found via CMake package")
else()
    aegon_find_required_library(
        TARGET      Aegon::Ext::deflate
        DISPLAY     "libdeflate"
        HEADER      libdeflate.h
        LIBRARIES   deflate libdeflate
        PKG_CONFIG  libdeflate
        ROOTS       LIBDEFLATE_ROOT DEFLATE_ROOT
        ARCH        libdeflate
        DEBIAN      libdeflate-dev
    )
endif()

# ------------------------------------------------------------------------------
# 8. HTTP/2: nghttp2
# ------------------------------------------------------------------------------
find_package(nghttp2 CONFIG QUIET)
if(TARGET nghttp2::nghttp2)
    add_library(Aegon::Ext::nghttp2 INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::nghttp2 INTERFACE nghttp2::nghttp2)
    message(STATUS "[Dependency] nghttp2 found via CMake package")
else()
    aegon_find_required_library(
        TARGET      Aegon::Ext::nghttp2
        DISPLAY     "nghttp2"
        HEADER      nghttp2/nghttp2.h
        LIBRARIES   nghttp2
        PKG_CONFIG  libnghttp2
        ROOTS       NGHTTP2_ROOT
        ARCH        libnghttp2
        DEBIAN      libnghttp2-dev
    )
endif()

# ------------------------------------------------------------------------------
# 9. HTTP/3 & QPACK: nghttp3
# ------------------------------------------------------------------------------
find_package(nghttp3 CONFIG QUIET)
if(TARGET nghttp3::nghttp3)
    add_library(Aegon::Ext::nghttp3 INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::nghttp3 INTERFACE nghttp3::nghttp3)
    message(STATUS "[Dependency] nghttp3 found via CMake package")
else()
    aegon_find_required_library(
        TARGET      Aegon::Ext::nghttp3
        DISPLAY     "nghttp3"
        HEADER      nghttp3/nghttp3.h
        LIBRARIES   nghttp3
        PKG_CONFIG  libnghttp3
        ROOTS       NGHTTP3_ROOT
        ARCH        libnghttp3
        DEBIAN      libnghttp3-dev
    )
endif()

# ------------------------------------------------------------------------------
# 10. QUIC: ngtcp2 & ngtcp2_crypto_ossl
# ------------------------------------------------------------------------------
find_package(ngtcp2 CONFIG QUIET)

if(TARGET ngtcp2::ngtcp2)
    add_library(Aegon::Ext::ngtcp2 INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::ngtcp2 INTERFACE ngtcp2::ngtcp2)
    message(STATUS "[Dependency] ngtcp2 found via CMake package")
else()
    aegon_find_required_library(
        TARGET      Aegon::Ext::ngtcp2
        DISPLAY     "ngtcp2"
        HEADER      ngtcp2/ngtcp2.h
        LIBRARIES   ngtcp2
        PKG_CONFIG  libngtcp2
        ROOTS       NGTCP2_ROOT
        ARCH        libngtcp2
        DEBIAN      libngtcp2-dev
    )
endif()

if(TARGET ngtcp2::ngtcp2_crypto_ossl)
    add_library(Aegon::Ext::ngtcp2_crypto INTERFACE IMPORTED GLOBAL)
    target_link_libraries(Aegon::Ext::ngtcp2_crypto INTERFACE ngtcp2::ngtcp2_crypto_ossl)
    message(STATUS "[Dependency] ngtcp2_crypto_ossl found via CMake package")
else()
    aegon_find_required_library(
        TARGET      Aegon::Ext::ngtcp2_crypto
        DISPLAY     "ngtcp2_crypto_ossl"
        HEADER      ngtcp2/ngtcp2_crypto_ossl.h
        LIBRARIES   ngtcp2_crypto_ossl
        PKG_CONFIG  libngtcp2_crypto_ossl
        ROOTS       NGTCP2_ROOT
        ARCH        libngtcp2
        DEBIAN      libngtcp2-dev
    )
endif()

# ------------------------------------------------------------------------------
# 11. Protobuf (native gRPC support and test code generation)
# ------------------------------------------------------------------------------
# Prefer pkg-config: on some distros the CMake package target omits the Abseil
# link dependencies that libprotobuf requires.
if(NOT TARGET Aegon::Ext::Protobuf)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(AEGON_PKG_PROTOBUF QUIET IMPORTED_TARGET protobuf)
        if(TARGET PkgConfig::AEGON_PKG_PROTOBUF)
            add_library(Aegon::Ext::Protobuf INTERFACE IMPORTED GLOBAL)
            target_link_libraries(Aegon::Ext::Protobuf INTERFACE PkgConfig::AEGON_PKG_PROTOBUF)
            message(STATUS "[Dependency] Protobuf found via pkg-config (${AEGON_PKG_PROTOBUF_VERSION})")
        endif()
    endif()
endif()

if(NOT TARGET Aegon::Ext::Protobuf)
    # Prefer the upstream CONFIG package: unlike the built-in module target, it
    # carries Protobuf's Abseil link dependencies.
    find_package(Protobuf CONFIG QUIET)
    if(NOT TARGET protobuf::libprotobuf)
        find_package(Protobuf QUIET)
    endif()

    if(TARGET protobuf::libprotobuf)
        add_library(Aegon::Ext::Protobuf INTERFACE IMPORTED GLOBAL)
        target_link_libraries(Aegon::Ext::Protobuf INTERFACE protobuf::libprotobuf)
        message(STATUS "[Dependency] Protobuf found via CMake package")
    elseif(Protobuf_FOUND AND Protobuf_LIBRARIES)
        add_library(Aegon::Ext::Protobuf INTERFACE IMPORTED GLOBAL)
        if(Protobuf_INCLUDE_DIRS)
            target_include_directories(Aegon::Ext::Protobuf INTERFACE ${Protobuf_INCLUDE_DIRS})
        endif()
        target_link_libraries(Aegon::Ext::Protobuf INTERFACE ${Protobuf_LIBRARIES})
        message(STATUS "[Dependency] Protobuf found via CMake package")
    else()
        aegon_find_required_library(
            TARGET      Aegon::Ext::Protobuf
            DISPLAY     "protobuf"
            HEADER      google/protobuf/message.h
            LIBRARIES   protobuf
            PKG_CONFIG  protobuf
            ROOTS       Protobuf_ROOT PROTOBUF_ROOT
            ARCH        protobuf
            DEBIAN      "libprotobuf-dev protobuf-compiler"
        )
    endif()
endif()

if(AEGON_BUILD_TESTS)
    find_program(AEGON_PROTOC_EXECUTABLE NAMES protoc)
    if(NOT AEGON_PROTOC_EXECUTABLE)
        message(FATAL_ERROR
            "Missing required test dependency: protoc\n"
            "  Arch:   sudo pacman -S protobuf\n"
            "  Debian: sudo apt install protobuf-compiler"
        )
    endif()
endif()
