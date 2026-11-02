# ==============================================================================
# Configuration System (aegon_config)
# ==============================================================================

if(TARGET Aegon::Ext::glaze)
    add_library(aegon_config
        DotEnv.cpp
        EnvExpander.cpp
    )
    target_link_libraries(aegon_config PUBLIC
        Aegon::Ext::glaze
    )
endif()
