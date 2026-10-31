# ==============================================================================
# Installation & CMake Package Export Configuration
# ==============================================================================

include(CMakePackageConfigHelpers)

if(AEGON_ACTIVE_TARGETS)
    install(
        TARGETS ${AEGON_ACTIVE_TARGETS}
        EXPORT AegonTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/aegon
    FILES_MATCHING
        PATTERN "c" EXCLUDE
        PATTERN "*.h"
        PATTERN "*.hpp"
)

if(AEGON_BUILD_C_ABI)
    install(
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/aegon/c
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/aegon
        FILES_MATCHING PATTERN "*.h"
    )

    set(AEGON_C_PRIVATE_LIBS "-laegon_http -laegon_net -laegon_core -laegon_log -lnghttp3 -lngtcp2 -lngtcp2_crypto_ossl -lnghttp2 -lssl -lcrypto -lz")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        string(APPEND AEGON_C_PRIVATE_LIBS " -luring -lpthread -lm -lstdc++")
    endif()
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/aegon-c.pc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/aegon-c.pc"
        @ONLY
    )
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/aegon-c.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
    )
endif()

install(
    EXPORT AegonTargets
    FILE AegonTargets.cmake
    NAMESPACE Aegon::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Aegon
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AegonConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/AegonConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Aegon
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/AegonConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/AegonConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/AegonConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Aegon
)
