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
#include "data/orm/sql/drivers/SqliteDriver.h"
#include "data/orm/sql/drivers/PostgresDriver.h"
#include "data/orm/sql/SqlDatabaseClient.h"
#include "data/redis/RedisClient.h"
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

    // Bootstrap Schema DDL
    {
        core::EventLoop init_loop;
        init_loop.spawn([&]() -> core::Task<void> {
            auto ddl_user = data::orm::sql::generate_ddl<User>(data::orm::sql::DatabaseDialect::SQLite);
            auto ddl_profile = data::orm::sql::generate_ddl<UserProfile>(data::orm::sql::DatabaseDialect::SQLite);
            auto ddl_prod = data::orm::sql::generate_ddl<Product>(data::orm::sql::DatabaseDialect::SQLite);
            auto ddl_order = data::orm::sql::generate_ddl<Order>(data::orm::sql::DatabaseDialect::SQLite);

            co_await sql_client->execute(ddl_user);
            co_await sql_client->execute(ddl_profile);
            co_await sql_client->execute(ddl_prod);
            co_await sql_client->execute(ddl_order);

            // Seed initial admin user and sample products
            User u1{.id = 1, .username = "alice", .email = "alice@aegon.dev", .balance = *data::types::Decimal128::from_string("1000.00")};
            co_await sql_client->insert(u1);

            Product p1{.id = 1, .sku = "VAL-SWORD", .name = "Valyrian Steel Sword", .price = *data::types::Decimal128::from_string("299.99"), .stock = 15, .version = 1};
            Product p2{.id = 2, .sku = "DRG-HELM", .name = "Dragon Scale Helmet", .price = *data::types::Decimal128::from_string("149.50"), .stock = 50, .version = 1};
            co_await sql_client->insert(p1);
            co_await sql_client->insert(p2);

            std::cout << "[DB Init] Schema created and initial catalog seeded.\n";
            co_return;
        }());
        init_loop.run();
    }

    // 2. Domain Services (leveraging ServiceRegistry and thread-local RedisProvider)
    auto catalog_service = std::make_shared<CatalogService>(*sql_client);
    auto order_service = std::make_shared<OrderService>(*sql_client);
    auto leaderboard_service = std::make_shared<LeaderboardService>(*sql_client);

    // 3. Configure HTTP Router
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

    // 4. Initialize Server & Register Services with ServiceRegistry
    http::Server server;
    server.provide(sql_client)
          .provide(catalog_service)
          .provide(order_service)
          .provide(leaderboard_service)
          .set_router(std::move(router))
          .listen(config.port, config.host);

    std::cout << "[Server] All services registered in ServiceRegistry. Starting server...\n";
    server.run();

    return 0;
}
