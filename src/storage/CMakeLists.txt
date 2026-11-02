# ==============================================================================
# Asynchronous Persistent Storage Engine (aegon_storage)
# ==============================================================================

add_library(aegon_storage
    DeviceDetector.cpp
    File.cpp
    FileStream.cpp
    WalLog.cpp
)

target_link_libraries(aegon_storage PUBLIC
    aegon_core
)
