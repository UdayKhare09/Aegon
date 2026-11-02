# ==============================================================================
# C ABI Wrapper Library (libaegon_c shared + static)
# ==============================================================================

set(AEGON_C_SOURCES
    error.cpp
    log.cpp
    request.cpp
    response.cpp
    context.cpp
    router.cpp
    server.cpp
    middleware.cpp
    cors.cpp
    compress.cpp
    security.cpp
    static_files.cpp
    multipart.cpp
    async.cpp
    websocket.cpp
    sse.cpp
)

# Static archive: libaegon_c.a (or aegon_c_static.lib on Windows)
add_library(aegon_c_static STATIC ${AEGON_C_SOURCES})
add_library(Aegon::c_static ALIAS aegon_c_static)
target_link_libraries(aegon_c_static PRIVATE aegon_http aegon_log)
target_compile_definitions(aegon_c_static PRIVATE AEGON_C_BUILDING)
target_compile_definitions(aegon_c_static PUBLIC AEGON_C_STATIC)
set_target_properties(aegon_c_static PROPERTIES
    OUTPUT_NAME aegon_c
    EXPORT_NAME c_static
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(aegon_c_static PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(aegon_c_static PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

# Shared library: libaegon_c.so / aegon_c.dll (for FFIs)
add_library(aegon_c_shared SHARED ${AEGON_C_SOURCES})
add_library(Aegon::c_shared ALIAS aegon_c_shared)
add_library(Aegon::c ALIAS aegon_c_shared)
add_library(aegon_c ALIAS aegon_c_shared)
target_link_libraries(aegon_c_shared PRIVATE aegon_http aegon_log)
target_compile_definitions(aegon_c_shared PRIVATE AEGON_C_BUILDING)
set_target_properties(aegon_c_shared PROPERTIES
    OUTPUT_NAME aegon_c
    EXPORT_NAME c
    VERSION   ${AEGON_VERSION_STRING}
    SOVERSION 1
    C_VISIBILITY_PRESET     hidden
    CXX_VISIBILITY_PRESET   hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(aegon_c_shared PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(aegon_c_shared PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_options(aegon_c_shared PRIVATE
    "LINKER:--version-script=${PROJECT_SOURCE_DIR}/cmake/aegon_c.map"
)

include(${PROJECT_SOURCE_DIR}/cmake/CheckCHeaders.cmake)
