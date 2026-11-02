# ==============================================================================
# High-Performance Telemetry & Logging (aegon_log)
# ==============================================================================

add_library(aegon_log
    ConsoleSink.cpp
    FileSink.cpp
    Logger.cpp
)

target_link_libraries(aegon_log PUBLIC
    Aegon::Ext::Threads
)
