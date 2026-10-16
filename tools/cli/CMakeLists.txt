# ==============================================================================
# Aegon Developer CLI Tool (aegon)
# ==============================================================================

if(TARGET aegon_core AND TARGET aegon_config)
    add_executable(aegon_cli main.cpp)
    set_target_properties(aegon_cli PROPERTIES OUTPUT_NAME aegon)

    target_include_directories(aegon_cli PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(aegon_cli PRIVATE
        Aegon::core
        Aegon::config
    )

    install(
        TARGETS aegon_cli
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
