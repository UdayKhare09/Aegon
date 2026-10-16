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
        PATTERN "*.h"
        PATTERN "*.hpp"
)

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
