#pragma once

#include "data/orm/sql/SqlDatabaseClient.h"
#include "data/redis/PerCoreRedisClient.h"
#include "dtos/OrderDto.h"
#include "core/Task.h"
#include <memory>
#include <vector>

namespace aegon::sample {

class LeaderboardService {
public:
    LeaderboardService(data::orm::sql::SqlDatabaseClient& db,
                       std::shared_ptr<data::redis::PerCoreRedisClient> redis = nullptr);

    core::Task<LeaderboardResponse> get_top_products(size_t limit);

private:
    data::orm::sql::SqlDatabaseClient& db_;
    std::shared_ptr<data::redis::PerCoreRedisClient> redis_;
};

} // namespace aegon::sample
