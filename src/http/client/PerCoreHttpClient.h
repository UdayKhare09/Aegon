#pragma once

#include "HttpClient.h"
#include "core/EventLoop.h"
#include <memory>
#include <unordered_map>
#include <string_view>
#include <utility>

namespace aegon::http::client {

/**
 * @brief Thread-per-core HTTP client wrapper.
 *
 * Lazily instantiates an isolated HttpClient instance with its own dedicated
 * connection pool for each worker thread / CPU core. Prevents cross-thread lock
 * contention and socket sharing across io_uring event loops.
 */
class PerCoreHttpClient {
public:
    explicit PerCoreHttpClient(ClientConfig config = {})
        : config_(std::move(config)) {}

    ~PerCoreHttpClient() {
        t_storage.clients.erase(this);
    }

    [[nodiscard]] HttpClient* current() const {
        auto it = t_storage.clients.find(this);
        if (it != t_storage.clients.end()) {
            return it->second.get();
        }

        auto client = std::make_unique<HttpClient>(config_);
        auto* ptr = client.get();
        t_storage.clients[this] = std::move(client);
        return ptr;
    }

    [[nodiscard]] HttpClient& get() const {
        return *current();
    }

    [[nodiscard]] HttpClient* operator->() const {
        return current();
    }

    // HTTP Verb Forwarders
    [[nodiscard]] RequestBuilder get(std::string_view url) const {
        return current()->get(url);
    }

    [[nodiscard]] RequestBuilder post(std::string_view url) const {
        return current()->post(url);
    }

    [[nodiscard]] RequestBuilder put(std::string_view url) const {
        return current()->put(url);
    }

    [[nodiscard]] RequestBuilder patch(std::string_view url) const {
        return current()->patch(url);
    }

    [[nodiscard]] RequestBuilder del(std::string_view url) const {
        return current()->del(url);
    }

    [[nodiscard]] RequestBuilder head(std::string_view url) const {
        return current()->head(url);
    }

    [[nodiscard]] RequestBuilder options(std::string_view url) const {
        return current()->options(url);
    }

    [[nodiscard]] RequestBuilder request(Method method, std::string_view url) const {
        return current()->request(method, url);
    }

    [[nodiscard]] const ClientConfig& config() const noexcept {
        return config_;
    }

private:
    ClientConfig config_{};

    struct Storage {
        std::unordered_map<const PerCoreHttpClient*, std::unique_ptr<HttpClient>> clients;
    };
    static inline thread_local Storage t_storage;
};

} // namespace aegon::http::client
