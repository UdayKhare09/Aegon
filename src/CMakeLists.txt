# ==============================================================================
# Aegon Source Modules
# ==============================================================================

# Core & Infrastructure
add_subdirectory(core)
add_subdirectory(log)
add_subdirectory(config)
add_subdirectory(net)
add_subdirectory(storage)

# Data Engines (memory, redis, orm)
add_subdirectory(data)

# Web Server Engine
add_subdirectory(http)

# Optional C ABI
if(AEGON_BUILD_C_ABI)
    add_subdirectory(c)
endif()

# ==============================================================================
# Include Directories, Aliases & Export Names
# ==============================================================================
set(AEGON_MODULES core log config net storage memory redis orm http)

set(AEGON_ACTIVE_TARGETS "")
foreach(mod ${AEGON_MODULES})
    if(TARGET aegon_${mod})
        set_target_properties(aegon_${mod} PROPERTIES EXPORT_NAME ${mod})
        add_library(Aegon::${mod} ALIAS aegon_${mod})
        target_include_directories(aegon_${mod} PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/aegon>
        )
        list(APPEND AEGON_ACTIVE_TARGETS aegon_${mod})
    endif()
endforeach()

if(AEGON_BUILD_C_ABI AND TARGET aegon_c_shared)
    list(APPEND AEGON_ACTIVE_TARGETS aegon_c_shared aegon_c_static)
endif()

# Propagate active targets to root scope for InstallAndExport
set(AEGON_ACTIVE_TARGETS "${AEGON_ACTIVE_TARGETS}" PARENT_SCOPE)
