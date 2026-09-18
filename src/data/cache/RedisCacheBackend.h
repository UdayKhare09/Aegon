#pragma once

#include "CacheBackend.h"
#include "data/redis/RedisClient.h"
#include <functional>
#include <memory>
#include <utility>

namespace aegon::data::cache {

class RedisCacheBackend : public CacheBackend {
public:
    using ClientProvider = std::function<redis::RedisClient*()>;

    explicit RedisCacheBackend(std::shared_ptr<redis::RedisClient> client, std::string prefix = "")
        : client_(std::move(client)), prefix_(std::move(prefix)) {}

    explicit RedisCacheBackend(ClientProvider provider, std::string prefix = "")
        : provider_(std::move(provider)), prefix_(std::move(prefix)) {}

    core::Task<std::optional<std::string>> get(std::string_view key) override {
        auto* c = get_client();
        if (!c) co_return std::nullopt;
        std::string full_key = make_key(key);
        co_return co_await c->get(full_key);
    }

    core::Task<std::vector<std::optional<std::string>>> mget(const std::vector<std::string>& keys) override {
        auto* c = get_client();
        if (!c) {
            co_return std::vector<std::optional<std::string>>(keys.size(), std::nullopt);
        }

        if (prefix_.empty()) {
            co_return co_await c->mget(keys);
        }

        std::vector<std::string> full_keys;
        full_keys.reserve(keys.size());
        for (const auto& k : keys) {
            full_keys.push_back(make_key(k));
        }
        co_return co_await c->mget(full_keys);
    }

    core::Task<bool> set(std::string_view key, std::string_view val, std::optional<std::chrono::seconds> ttl = std::nullopt) override {
        auto* c = get_client();
        if (!c) co_return false;
        std::string full_key = make_key(key);
        co_return co_await c->set(full_key, val, ttl);
    }

    core::Task<bool> del(std::string_view key) override {
        auto* c = get_client();
        if (!c) co_return false;
        std::string full_key = make_key(key);
        co_return co_await c->del(full_key);
    }

    core::Task<int64_t> del_many(const std::vector<std::string>& keys) override {
        auto* c = get_client();
        if (!c) co_return 0;

        if (prefix_.empty()) {
            co_return co_await c->del_many(keys);
        }

        std::vector<std::string> full_keys;
        full_keys.reserve(keys.size());
        for (const auto& k : keys) {
            full_keys.push_back(make_key(k));
        }
        co_return co_await c->del_many(full_keys);
    }

    core::Task<int64_t> incr(std::string_view key) override {
        auto* c = get_client();
        if (!c) co_return 0;
        std::string full_key = make_key(key);
        co_return co_await c->incr(full_key);
    }

    [[nodiscard]] const std::shared_ptr<redis::RedisClient>& client() const noexcept { return client_; }

private:
    [[nodiscard]] redis::RedisClient* get_client() const noexcept {
        if (provider_) return provider_();
        return client_.get();
    }

    std::string make_key(std::string_view key) const {
        if (prefix_.empty()) return std::string(key);
        std::string res;
        res.reserve(prefix_.size() + key.size());
        res.append(prefix_);
        res.append(key);
        return res;
    }

    std::shared_ptr<redis::RedisClient> client_;
    ClientProvider provider_{nullptr};
    std::string prefix_;
};

} // namespace aegon::data::cache
