#include "services/OrderService.h"
#include "services/RedisProvider.h"
#include "data/redis/RedisLock.h"
#include <iostream>

namespace aegon::sample {

OrderService::OrderService(data::orm::sql::SqlDatabaseClient& db,
                           std::shared_ptr<data::redis::RedisClient> redis)
    : db_(db), redis_(std::move(redis)) {}

core::Task<OrderCreationResult> OrderService::place_order(CreateOrderRequest req) {
    auto* redis = redis_ ? redis_.get() : get_current_redis_client();

    // 1. Acquire Distributed Mutex for Flash-Sale concurrency control if Redis is present
    std::optional<data::redis::RedisLock> dist_lock;
    if (redis) {
        dist_lock = co_await redis->lock(
            "lock:product:" + std::to_string(req.product_id),
            std::chrono::milliseconds(4000),
            std::chrono::milliseconds(50),
            20 // max 20 retries
        );
        if (!dist_lock.has_value()) {
            co_return OrderCreationResult{false, "High concurrency contention: failed to acquire product mutex.", std::nullopt};
        }
    }

    // 2. Fetch User
    auto user = co_await db_.find_by_id<User>(req.user_id);
    if (!user) {
        co_return OrderCreationResult{false, "User not found.", std::nullopt};
    }

    // 3. Fetch Product
    auto product = co_await db_.find_by_id<Product>(req.product_id);
    if (!product) {
        co_return OrderCreationResult{false, "Product not found.", std::nullopt};
    }

    // 4. Validate stock availability
    if (product->stock < req.quantity) {
        co_return OrderCreationResult{false, "Insufficient inventory.", std::nullopt};
    }

    // 5. Validate user balance
    data::types::Decimal128 total_cost = product->price * req.quantity;
    if (user->balance < total_cost) {
        co_return OrderCreationResult{false, "Insufficient user balance.", std::nullopt};
    }

    // 6. Execute Atomic Transaction with Optimistic Concurrency Control (OCC)
    Order created_order;
    bool tx_ok = false;
    std::string err_msg;

    co_await db_.transaction([&](data::orm::sql::Transaction& tx) -> core::Task<void> {
        // A. Decrement product stock (OCC automatically checks version column)
        product->stock -= req.quantity;
        bool prod_ok = co_await tx.update_entity(*product);
        if (!prod_ok) {
            err_msg = "Optimistic Concurrency Control (OCC) conflict: product was modified concurrently.";
            co_await tx.rollback();
            co_return;
        }

        // B. Deduct balance from user
        user->balance = user->balance - total_cost;
        bool user_ok = co_await tx.update_entity(*user);
        if (!user_ok) {
            err_msg = "Failed to update user balance.";
            co_await tx.rollback();
            co_return;
        }

        // C. Record Order
        Order ord;
        ord.user_id = req.user_id;
        ord.product_id = req.product_id;
        ord.quantity = req.quantity;
        ord.total_amount = total_cost;
        ord.status = "CONFIRMED";
        ord.created_at = data::types::DateTime::now();

        int64_t new_id = co_await tx.insert_get_id(ord);
        ord.id = new_id;
        created_order = ord;
        tx_ok = true;
    });

    if (!tx_ok) {
        co_return OrderCreationResult{false, err_msg.empty() ? "Transaction failed." : err_msg, std::nullopt};
    }

    // 7. Post-Transaction Distributed Side Effects (Redis Cache Invalidation, Sorted Set, Streams)
    if (redis) {
        // Invalidate Product Cache
        (void)co_await redis->del("product:" + std::to_string(req.product_id));

        // Atomic Lua Script to update Top-Selling Leaderboard via ZINCRBY
        std::string script = "return redis.call('ZINCRBY', KEYS[1], ARGV[1], ARGV[2])";
        (void)co_await redis->eval(
            script,
            {"leaderboard:products"},
            {std::to_string(req.quantity), std::to_string(req.product_id)}
        );

        // Publish Order Event to Redis Stream for background processing
        (void)co_await redis->xadd(
            "stream:orders",
            "*",
            {
                {"event", "order_created"},
                {"order_id", std::to_string(created_order.id)},
                {"product_id", std::to_string(req.product_id)},
                {"quantity", std::to_string(req.quantity)},
                {"total_amount", total_cost.to_string()}
            }
        );
    }

    co_return OrderCreationResult{true, "Order confirmed successfully.", created_order};
}

core::Task<std::optional<Order>> OrderService::get_order_by_id(int64_t id) {
    co_return co_await db_.find_by_id<Order>(id);
}

} // namespace aegon::sample
