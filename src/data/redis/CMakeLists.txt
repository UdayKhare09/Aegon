# ==============================================================================
# Redis Client (aegon_redis)
# ==============================================================================

add_library(aegon_redis
    Resp3.cpp
    RedisConnection.cpp
    RedisClient.cpp
    RedisLock.cpp
)

target_link_libraries(aegon_redis PUBLIC
    aegon_net
    ${AEGON_CRYPTO_LIB}
)
