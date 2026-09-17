#pragma once

#include "core/Task.h"
#include <string>
#include <chrono>
#include <utility>

namespace aegon::data::redis {

class RedisClient;

class RedisLock {
public:
    RedisLock(RedisClient& client, std::string key, std::string token, std::chrono::milliseconds ttl)
        : client_(&client), key_(std::move(key)), token_(std::move(token)), ttl_(ttl), locked_(true) {}

    RedisLock(const RedisLock&) = delete;
    RedisLock& operator=(const RedisLock&) = delete;

    RedisLock(RedisLock&& other) noexcept
        : client_(other.client_), key_(std::move(other.key_)), token_(std::move(other.token_)),
          ttl_(other.ttl_), locked_(other.locked_) {
        other.locked_ = false;
        other.client_ = nullptr;
    }

    RedisLock& operator=(RedisLock&& other) noexcept {
        if (this != &other) {
            client_ = other.client_;
            key_ = std::move(other.key_);
            token_ = std::move(other.token_);
            ttl_ = other.ttl_;
            locked_ = other.locked_;
            other.locked_ = false;
            other.client_ = nullptr;
        }
        return *this;
    }

    ~RedisLock() = default;

    [[nodiscard]] bool locked() const noexcept { return locked_; }
    [[nodiscard]] const std::string& key() const noexcept { return key_; }
    [[nodiscard]] const std::string& token() const noexcept { return token_; }
    [[nodiscard]] std::chrono::milliseconds ttl() const noexcept { return ttl_; }

    core::Task<bool> extend(std::chrono::milliseconds extra_ttl);
    core::Task<bool> release();

private:
    RedisClient* client_{nullptr};
    std::string key_;
    std::string token_;
    std::chrono::milliseconds ttl_{0};
    bool locked_{false};
};

} // namespace aegon::data::redis
