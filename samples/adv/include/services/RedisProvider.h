#pragma once

#include "core/EventLoop.h"
#include "data/redis/RedisClient.h"
#include <memory>

namespace aegon::sample {

inline data::redis::RedisClient* get_current_redis_client() {
    static thread_local std::unique_ptr<data::redis::RedisClient> t_redis;
    auto* loop = core::EventLoop::current();
    if (!loop) return nullptr;
    if (!t_redis) {
        data::redis::RedisNodeConfig cfg{
            .host = "127.0.0.1",
            .port = 6379,
            .password = "redis_secret"
        };
        t_redis = std::make_unique<data::redis::RedisClient>(loop->ring(), cfg, 4);
    }
    return t_redis.get();
}

} // namespace aegon::sample
