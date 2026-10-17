vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO UdayKhare09/Aegon
    REF v0.2.1
    SHA512 0
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DAEGON_BUILD_TESTS=OFF
        -DAEGON_BUILD_SAMPLES=OFF
        -DAEGON_BUILD_BENCHMARKS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME Aegon CONFIG_PATH lib/cmake/Aegon)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
