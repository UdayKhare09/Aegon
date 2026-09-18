#pragma once

#include "data/orm/sql/SqlDatabaseClient.h"
#include "data/redis/RedisClient.h"
#include "models/Order.h"
#include "models/Product.h"
#include "models/User.h"
#include "dtos/OrderDto.h"
#include "core/Task.h"
#include <memory>
#include <string>
#include <optional>

namespace aegon::sample {

struct OrderCreationResult {
    bool success{false};
    std::string message;
    std::optional<Order> order;
};

class OrderService {
public:
    OrderService(data::orm::sql::SqlDatabaseClient& db,
                 std::shared_ptr<data::redis::RedisClient> redis = nullptr);

    /**
     * @brief Creates an order with Distributed Mutex, OCC validation, SQL Transaction,
     * Redis Cache Invalidation, Sorted Set Leaderboard increment, and Redis Stream publishing.
     */
    core::Task<OrderCreationResult> place_order(CreateOrderRequest req);

    core::Task<std::optional<Order>> get_order_by_id(int64_t id);

private:
    data::orm::sql::SqlDatabaseClient& db_;
    std::shared_ptr<data::redis::RedisClient> redis_;
};

} // namespace aegon::sample
