#pragma once

#include "RedisClient.h"
#include "core/EventLoop.h"
#include <memory>
#include <unordered_map>
#include <variant>
#include <functional>
#include <stdexcept>

namespace aegon::data::redis {

class PerCoreRedisClient {
public:
    using ConfigVariant = std::variant<RedisNodeConfig, SentinelConfig, ClusterConfig>;

    explicit PerCoreRedisClient(RedisNodeConfig config, size_t pool_size = 8)
        : config_(std::move(config)), pool_size_(pool_size), mode_(RedisMode::Standalone) {}

    explicit PerCoreRedisClient(SentinelConfig config, size_t pool_size = 8)
        : config_(std::move(config)), pool_size_(pool_size), mode_(RedisMode::Sentinel) {}

    explicit PerCoreRedisClient(ClusterConfig config)
        : config_(std::move(config)), pool_size_(0), mode_(RedisMode::Cluster) {}

    [[nodiscard]] RedisClient* current() const {
        auto* loop = core::EventLoop::current();
        if (!loop) return nullptr;

        struct Storage {
            std::unordered_map<const PerCoreRedisClient*, std::unique_ptr<RedisClient>> clients;
        };
        static thread_local Storage t_storage;

        auto it = t_storage.clients.find(this);
        if (it != t_storage.clients.end()) {
            return it->second.get();
        }

        std::unique_ptr<RedisClient> client;
        if (mode_ == RedisMode::Standalone) {
            client = std::make_unique<RedisClient>(loop->ring(), std::get<RedisNodeConfig>(config_), pool_size_);
        } else if (mode_ == RedisMode::Sentinel) {
            client = std::make_unique<RedisClient>(loop->ring(), std::get<SentinelConfig>(config_), pool_size_);
        } else if (mode_ == RedisMode::Cluster) {
            client = std::make_unique<RedisClient>(loop->ring(), std::get<ClusterConfig>(config_));
        }

        auto* ptr = client.get();
        t_storage.clients[this] = std::move(client);
        return ptr;
    }

    [[nodiscard]] RedisClient& get() const {
        auto* c = current();
        if (!c) {
            throw std::runtime_error("PerCoreRedisClient::get() called outside of an active EventLoop");
        }
        return *c;
    }

    [[nodiscard]] RedisClient* operator->() const {
        return &get();
    }

    [[nodiscard]] RedisClient& operator*() const {
        return get();
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return current() != nullptr;
    }

    [[nodiscard]] std::function<RedisClient*()> provider() const {
        return [this]() -> RedisClient* {
            return this->current();
        };
    }

private:
    ConfigVariant config_;
    size_t pool_size_{8};
    RedisMode mode_{RedisMode::Standalone};
};

} // namespace aegon::data::redis
