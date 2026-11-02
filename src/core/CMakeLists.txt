# ==============================================================================
# Async I/O Engine Core (aegon_core)
# ==============================================================================

set(AEGON_CORE_SOURCES
    BufferPool.cpp
    ThreadPool.cpp
    engine/IoEngine.cpp
    EventLoop.cpp
)

if(WIN32)
    list(APPEND AEGON_CORE_SOURCES
        engine/IocpEngine.cpp
    )
    add_library(aegon_core ${AEGON_CORE_SOURCES})
    target_link_libraries(aegon_core
        PUBLIC
            Threads::Threads
        PRIVATE
            ws2_32
            mswsock
            ntdll
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND AEGON_CORE_SOURCES
        engine/IoUringEngine.cpp
        engine/EpollEngine.cpp
    )
    add_library(aegon_core ${AEGON_CORE_SOURCES})
    target_link_libraries(aegon_core
        PUBLIC
            Threads::Threads
        PRIVATE
            uring
    )
else()
    add_library(aegon_core ${AEGON_CORE_SOURCES})
    target_link_libraries(aegon_core
        PUBLIC
            Threads::Threads
    )
endif()

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
