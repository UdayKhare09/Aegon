#include "config/AppConfig.h"
#include "models/User.h"
#include "models/Product.h"
#include "models/Order.h"
#include "services/CatalogService.h"
#include "services/OrderService.h"
#include "services/LeaderboardService.h"
#include "services/EventStreamWorker.h"
#include "handlers/AuthHandler.h"
#include "handlers/CatalogHandler.h"
#include "handlers/OrderHandler.h"
#include "handlers/LeaderboardHandler.h"
#include "handlers/StreamHandler.h"

#include "http/Server.h"
#include "data/cache/RedisCacheBackend.h"
#include "data/redis/PerCoreRedisClient.h"
#include "data/orm/sql/drivers/SqliteDriver.h"
#include "data/orm/sql/drivers/PostgresDriver.h"
#include "data/orm/sql/SqlDatabaseClient.h"
#include "core/EventLoop.h"

#include <iostream>
#include <memory>

using namespace aegon;
using namespace aegon::sample;

int main(int argc, char* argv[]) {
    AppConfig config;
    if (argc > 1) {
        config.port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    std::cout << "========================================================\n";
    std::cout << "      Aegon HyperStore — Advanced Microservice Sample   \n";
    std::cout << "========================================================\n";
    std::cout << "Listening on port: " << config.port << "\n";

    // 1. Initialize SQLite Database Pool & ORM Client
    auto pool = data::orm::sql::drivers::create_sqlite_pool(":memory:", 4);
    auto sql_client = std::make_shared<data::orm::sql::SqlDatabaseClient>(*pool);

    // 2. Initialize Per-Core Redis Client & attach as declarative L2 cache for SQL ORM
    auto redis_client = std::make_shared<data::redis::PerCoreRedisClient>(data::redis::RedisNodeConfig{
        .host = "127.0.0.1",
        .port = 6379,
        .password = "redis_secret"
    }, 4);
    auto sql_cache = std::make_shared<data::cache::RedisCacheBackend>(redis_client->provider());
    sql_client->set_cache(sql_cache);

    // 3. Domain Services (injected with SQL client and PerCoreRedisClient)
    auto catalog_service = std::make_shared<CatalogService>(*sql_client);
    auto order_service = std::make_shared<OrderService>(*sql_client, redis_client);
    auto leaderboard_service = std::make_shared<LeaderboardService>(*sql_client, redis_client);

    // 4. Configure HTTP Router
    http::Router router;

    // Health Check
    router.get("/health", [](http::Context& ctx) -> core::Task<void> {
        ctx.res().status(http::StatusCode::Ok).text("OK");
        co_return;
    });

    // Authentication / Users
    router.post("/api/v1/users", AuthHandler::register_user);
    router.get("/api/v1/users/:id", AuthHandler::get_user);

    // Product Catalog
    router.post("/api/v1/products", CatalogHandler::create_product);
    router.get("/api/v1/products", CatalogHandler::list_products);
    router.get("/api/v1/products/:id", CatalogHandler::get_product);

    // Flash-Sale Orders
    router.post("/api/v1/orders", OrderHandler::place_order);
    router.get("/api/v1/orders/:id", OrderHandler::get_order);

    // Real-Time Leaderboard
    router.get("/api/v1/leaderboard", LeaderboardHandler::get_top_products);

    // SSE Live Stream
    router.get("/api/v1/events/live", StreamHandler::live_events);

    // 5. Initialize Server & Register Services with ServiceRegistry
    http::Server server;
    server.provide(sql_client)
          .provide(redis_client)
          .provide(catalog_service)
          .provide(order_service)
          .provide(leaderboard_service)
          .on_start([](http::Server& s) -> core::Task<void> {
              auto sql = s.service<data::orm::sql::SqlDatabaseClient>();

              // Asynchronous schema migration & seed data before accepting traffic
              co_await sql->sync_schema<User, UserProfile, Product, Order>();

              User u1{.id = 1, .username = "alice", .email = "alice@aegon.dev", .balance = *data::types::Decimal128::from_string("1000.00")};
              co_await sql->insert(u1);

              Product p1{.id = 1, .sku = "VAL-SWORD", .name = "Valyrian Steel Sword", .price = *data::types::Decimal128::from_string("299.99"), .stock = 15, .version = 1};
              Product p2{.id = 2, .sku = "DRG-HELM", .name = "Dragon Scale Helmet", .price = *data::types::Decimal128::from_string("149.50"), .stock = 50, .version = 1};
              co_await sql->insert(p1);
              co_await sql->insert(p2);

              std::cout << "[Server:on_start] Schema migrated and initial catalog seeded.\n";
              co_return;
          })
          .set_router(std::move(router))
          .listen(config.port, config.host);

    std::cout << "[Server] All services registered in ServiceRegistry. Starting server...\n";
    server.run();

    return 0;
}
