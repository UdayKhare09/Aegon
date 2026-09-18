#include "services/LeaderboardService.h"
#include "models/Product.h"
#include <iostream>

namespace aegon::sample {

LeaderboardService::LeaderboardService(data::orm::sql::SqlDatabaseClient& db,
                                       std::shared_ptr<data::redis::PerCoreRedisClient> redis)
    : db_(db), redis_(std::move(redis)) {}

core::Task<LeaderboardResponse> LeaderboardService::get_top_products(size_t limit) {
    LeaderboardResponse resp;

    auto* redis = redis_ ? redis_->current() : nullptr;
    if (!redis) {
        co_return resp;
    }

    // Query Redis Sorted Set for top scorers in descending order
    auto top_items = co_await redis->zrevrange_with_scores(
        "leaderboard:products",
        0,
        static_cast<int64_t>(limit > 0 ? limit - 1 : 0)
    );

    for (const auto& [member, score] : top_items) {
        int64_t prod_id = 0;
        try {
            prod_id = std::stoll(member);
        } catch (...) {
            continue;
        }

        auto prod = co_await db_.find_by_id<Product>(prod_id);
        resp.top_products.push_back(LeaderboardEntry{
            .product_id = prod_id,
            .sku = prod ? prod->sku : "UNKNOWN",
            .sales_volume = score
        });
    }

    co_return resp;
}

} // namespace aegon::sample
