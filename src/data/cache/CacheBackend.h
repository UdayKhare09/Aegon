#pragma once

#include "core/Task.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <span>
#include <chrono>

namespace aegon::data::cache {

class CacheBackend {
public:
    virtual ~CacheBackend() = default;

    virtual core::Task<std::optional<std::string>> get(std::string_view key) = 0;
    virtual core::Task<std::vector<std::optional<std::string>>> mget(const std::vector<std::string>& keys) = 0;
    virtual core::Task<bool> set(std::string_view key, std::string_view val, std::optional<std::chrono::seconds> ttl = std::nullopt) = 0;
    virtual core::Task<bool> del(std::string_view key) = 0;
    virtual core::Task<int64_t> del_many(const std::vector<std::string>& keys) = 0;
    virtual core::Task<int64_t> incr(std::string_view key) = 0;
};

} // namespace aegon::data::cache
