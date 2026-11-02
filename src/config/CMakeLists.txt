# ==============================================================================
# Configuration System (aegon_config)
# ==============================================================================

if(TARGET glaze::glaze)
    add_library(aegon_config
        DotEnv.cpp
        EnvExpander.cpp
    )
    target_link_libraries(aegon_config PUBLIC
        glaze::glaze
    )
endif()
