# ==============================================================================
# In-Memory Store (aegon_memory)
# ==============================================================================

add_library(aegon_memory
    MemStore.cpp
)

target_link_libraries(aegon_memory PUBLIC
    aegon_core
)
