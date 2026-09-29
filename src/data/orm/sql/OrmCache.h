#pragma once

#include "core/Task.h"
#include "data/cache/CacheBackend.h"
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <chrono>

namespace aegon::data::orm::sql {

enum class DeferredCacheOpType {
    Del,
    Incr,
    Set
};

struct DeferredCacheOp {
    DeferredCacheOpType type;
    std::string key;
    std::string value{};
    std::chrono::seconds ttl{300};
};

std::string format_cache_id_key(std::string_view table, std::string_view id);
std::string format_cache_unique_key(std::string_view table, std::string_view col, std::string_view val);
std::string format_cache_epoch_key(std::string_view table);
std::string format_cache_partition_epoch_key(std::string_view table, std::string_view part_val);
std::string format_cache_pred_epoch_key(std::string_view table, std::string_view col, std::string_view val);

core::Task<void> flush_deferred_cache(cache::CacheBackend& cache, std::span<const DeferredCacheOp> ops);

} // namespace aegon::data::orm::sql
