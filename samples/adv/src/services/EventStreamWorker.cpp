#include "services/EventStreamWorker.h"
#include <iostream>

namespace aegon::sample {

EventStreamWorker::EventStreamWorker(std::shared_ptr<data::redis::RedisClient> redis)
    : redis_(std::move(redis)) {}

core::Task<void> EventStreamWorker::run() {
    if (!redis_) {
        co_return;
    }

    std::string last_id = "0-0";
    while (running_) {
        try {
            auto results = co_await redis_->xread(
                {"stream:orders"},
                {last_id},
                10,
                std::chrono::milliseconds(1000)
            );

            for (const auto& stream_res : results) {
                for (const auto& msg : stream_res.messages) {
                    last_id = msg.id;
                    std::cout << "[OrderStreamWorker] Processed event ID: " << msg.id;
                    for (const auto& [k, v] : msg.fields) {
                        std::cout << " | " << k << "=" << v;
                    }
                    std::cout << "\n";
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[OrderStreamWorker] Error polling stream: " << e.what() << "\n";
            break;
        }
    }
}

void EventStreamWorker::stop() noexcept {
    running_ = false;
}

} // namespace aegon::sample
