#include "OrmCache.h"

namespace aegon::data::orm::sql {

std::string format_cache_id_key(std::string_view table, std::string_view id) {
    std::string s;
    s.reserve(table.size() + 4 + id.size());
    s.append(table);
    s.append(":id:");
    s.append(id);
    return s;
}

std::string format_cache_unique_key(std::string_view table, std::string_view col, std::string_view val) {
    std::string s;
    s.reserve(table.size() + 1 + col.size() + 1 + val.size());
    s.append(table);
    s.push_back(':');
    s.append(col);
    s.push_back(':');
    s.append(val);
    return s;
}

std::string format_cache_epoch_key(std::string_view table) {
    std::string s;
    s.reserve(table.size() + 6);
    s.append(table);
    s.append(":epoch");
    return s;
}

std::string format_cache_partition_epoch_key(std::string_view table, std::string_view part_val) {
    std::string s;
    s.reserve(table.size() + 6 + part_val.size() + 6);
    s.append(table);
    s.append(":part:");
    s.append(part_val);
    s.append(":epoch");
    return s;
}

std::string format_cache_pred_epoch_key(std::string_view table, std::string_view col, std::string_view val) {
    std::string s;
    s.reserve(table.size() + 6 + col.size() + 1 + val.size() + 6);
    s.append(table);
    s.append(":pred:");
    s.append(col);
    s.push_back(':');
    s.append(val);
    s.append(":epoch");
    return s;
}

core::Task<void> flush_deferred_cache(cache::CacheBackend& cache, std::span<const DeferredCacheOp> ops) {
    for (const auto& op : ops) {
        if (op.type == DeferredCacheOpType::Del) {
            co_await cache.del(op.key);
        } else if (op.type == DeferredCacheOpType::Incr) {
            co_await cache.incr(op.key);
        } else if (op.type == DeferredCacheOpType::Set) {
            co_await cache.set(op.key, op.value, op.ttl);
        }
    }
}

} // namespace aegon::data::orm::sql
