# ==============================================================================
# Aegon Third-Party Dependencies Resolution
# ==============================================================================

# Core Threads
find_package(Threads REQUIRED)

# Serialization: Glaze
find_package(glaze REQUIRED)

# Cryptography & TLS: OpenSSL
find_package(OpenSSL QUIET)
if(TARGET OpenSSL::Crypto)
    set(AEGON_CRYPTO_LIB OpenSSL::Crypto)
    set(AEGON_SSL_LIB OpenSSL::SSL)
else()
    set(AEGON_CRYPTO_LIB crypto)
    set(AEGON_SSL_LIB ssl)
endif()

# Database: PostgreSQL
find_package(PostgreSQL QUIET)
if(TARGET PostgreSQL::PostgreSQL)
    set(AEGON_POSTGRES_LIB PostgreSQL::PostgreSQL)
elseif(PostgreSQL_LIBRARIES)
    set(AEGON_POSTGRES_LIB ${PostgreSQL_LIBRARIES})
else()
    set(AEGON_POSTGRES_LIB pq)
endif()

# Database: SQLite3
find_package(SQLite3 QUIET)
if(TARGET SQLite3::SQLite3)
    set(AEGON_SQLITE_LIB SQLite3::SQLite3)
elseif(TARGET SQLite::SQLite3)
    set(AEGON_SQLITE_LIB SQLite::SQLite3)
else()
    find_package(unofficial-sqlite3 CONFIG QUIET)
    if(TARGET unofficial::sqlite3::sqlite3)
        set(AEGON_SQLITE_LIB unofficial::sqlite3::sqlite3)
    else()
        set(AEGON_SQLITE_LIB sqlite3)
    endif()
endif()

# Compression: libdeflate
find_package(libdeflate CONFIG QUIET)
if(TARGET libdeflate::libdeflate_shared)
    set(AEGON_DEFLATE_LIB libdeflate::libdeflate_shared)
elseif(TARGET libdeflate::libdeflate_static)
    set(AEGON_DEFLATE_LIB libdeflate::libdeflate_static)
elseif(TARGET libdeflate::libdeflate)
    set(AEGON_DEFLATE_LIB libdeflate::libdeflate)
else()
    find_library(LIBDEFLATE_LIB NAMES deflate libdeflate QUIET)
    if(LIBDEFLATE_LIB)
        set(AEGON_DEFLATE_LIB ${LIBDEFLATE_LIB})
    else()
        set(AEGON_DEFLATE_LIB deflate)
    endif()
endif()

# HTTP/2: nghttp2
find_package(nghttp2 CONFIG QUIET)
if(TARGET nghttp2::nghttp2)
    set(AEGON_NGHTTP2_LIB nghttp2::nghttp2)
else()
    find_library(NGHTTP2_LIB NAMES nghttp2 QUIET)
    if(NGHTTP2_LIB)
        set(AEGON_NGHTTP2_LIB ${NGHTTP2_LIB})
    else()
        set(AEGON_NGHTTP2_LIB nghttp2)
    endif()
endif()

# HTTP/3 & QPACK: nghttp3
find_package(nghttp3 CONFIG QUIET)
if(TARGET nghttp3::nghttp3)
    set(AEGON_NGHTTP3_LIB nghttp3::nghttp3)
else()
    find_library(NGHTTP3_LIB NAMES nghttp3 QUIET)
    if(NGHTTP3_LIB)
        set(AEGON_NGHTTP3_LIB ${NGHTTP3_LIB})
    else()
        set(AEGON_NGHTTP3_LIB nghttp3)
    endif()
endif()

# QUIC: ngtcp2 & ngtcp2_crypto_ossl
find_package(ngtcp2 CONFIG QUIET)
if(TARGET ngtcp2::ngtcp2_crypto_ossl)
    set(AEGON_NGTCP2_CRYPTO_LIB ngtcp2::ngtcp2_crypto_ossl)
else()
    find_library(NGTCP2_CRYPTO_LIB NAMES ngtcp2_crypto_ossl QUIET)
    if(NGTCP2_CRYPTO_LIB)
        set(AEGON_NGTCP2_CRYPTO_LIB ${NGTCP2_CRYPTO_LIB})
    else()
        set(AEGON_NGTCP2_CRYPTO_LIB ngtcp2_crypto_ossl)
    endif()
endif()

if(TARGET ngtcp2::ngtcp2)
    set(AEGON_NGTCP2_LIB ngtcp2::ngtcp2)
else()
    find_library(NGTCP2_LIB NAMES ngtcp2 QUIET)
    if(NGTCP2_LIB)
        set(AEGON_NGTCP2_LIB ${NGTCP2_LIB})
    else()
        set(AEGON_NGTCP2_LIB ngtcp2)
    endif()
endif()
