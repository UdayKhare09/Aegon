#pragma once

#include "data/redis/RedisClient.h"
#include "core/Task.h"
#include <memory>
#include <atomic>

namespace aegon::sample {

class EventStreamWorker {
public:
    explicit EventStreamWorker(std::shared_ptr<data::redis::RedisClient> redis);

    core::Task<void> run();
    void stop() noexcept;

private:
    std::shared_ptr<data::redis::RedisClient> redis_;
    std::atomic<bool> running_{true};
};

} // namespace aegon::sample
