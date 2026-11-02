# ==============================================================================
# Async I/O Engine Core (aegon_core)
# ==============================================================================

add_library(aegon_core
    BufferPool.cpp
    ThreadPool.cpp
    EventLoop.cpp
    engine/IoEngine.cpp
    engine/IoUringEngine.cpp
    engine/EpollEngine.cpp
)

target_link_libraries(aegon_core
    PUBLIC
        Threads::Threads
    PRIVATE
        uring
)

target_precompile_headers(aegon_core PRIVATE
    <vector>
    <string>
    <string_view>
    <span>
    <memory>
    <optional>
    <chrono>
    <coroutine>
)
