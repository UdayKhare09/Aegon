cmake_minimum_required(VERSION 3.28)

add_executable(aegon_adv_sample
    src/main.cpp
    src/services/CatalogService.cpp
    src/services/OrderService.cpp
    src/services/LeaderboardService.cpp
    src/services/EventStreamWorker.cpp
    src/handlers/AuthHandler.cpp
    src/handlers/CatalogHandler.cpp
    src/handlers/OrderHandler.cpp
    src/handlers/LeaderboardHandler.cpp
    src/handlers/StreamHandler.cpp
)

target_include_directories(aegon_adv_sample PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(aegon_adv_sample PRIVATE
    Aegon::http
    Aegon::orm
    Aegon::redis
)
