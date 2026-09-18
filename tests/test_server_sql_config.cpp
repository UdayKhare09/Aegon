#include "http/Server.h"
#include "data/orm/sql/Sql.h"
#include "data/orm/sql/drivers/SqliteDriver.h"
#include "data/orm/sql/drivers/PostgresDriver.h"
#include <iostream>
#include <cassert>
#include <string>

#define TEST_CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace aegon::core;
using namespace aegon::http;
using namespace aegon::data::orm::sql;

struct ServerItem {
    int64_t id{0};
    std::string name;
    int32_t quantity{0};

    static auto schema() {
        return table<ServerItem>("server_items")
            .id(&ServerItem::id)
            .column(&ServerItem::name, "name")
            .column(&ServerItem::quantity, "quantity");
    }
};

void test_sqlite_in_memory_config() {
    std::cout << "[TEST 1] Testing Server.provide with SQLite in-memory and Context auto-wiring...\n";

    auto pool = drivers::create_sqlite_pool(":memory:", 2);
    auto client = std::make_shared<SqlDatabaseClient>(*pool);

    Server server;
    server.provide(client);

    TEST_CHECK(server.services().has<SqlDatabaseClient>());

    Router router;
    bool handler_called = false;

    router.post("/items", [&](Context& ctx) -> Task<void> {
        handler_called = true;
        TEST_CHECK(ctx.has_service<SqlDatabaseClient>());
        auto& db = ctx.service<SqlDatabaseClient>();

        // 1. Create table
        auto ddl = generate_ddl<ServerItem>(DatabaseDialect::SQLite);
        co_await db.execute(ddl);

        // 2. Direct insert
        ServerItem item1{.id = 1, .name = "Aegon Shield", .quantity = 10};
        co_await db.insert(item1);

        // 3. Find by ID
        auto found = co_await db.find_by_id<ServerItem>(int64_t{1});
        TEST_CHECK(found.has_value());
        TEST_CHECK(found->name == "Aegon Shield");
        TEST_CHECK(found->quantity == 10);

        // 4. Option 4 Transaction
        co_await db.transaction([](Transaction& tx) -> Task<void> {
            ServerItem item2{.id = 2, .name = "Dragon Helm", .quantity = 5};
            co_await tx.insert(item2);
            co_return;
        });

        // 5. Fetch all
        auto all_items = co_await db.fetch_all(db.from<ServerItem>());
        TEST_CHECK(all_items.size() == 2);

        ctx.res().status(StatusCode::Ok).text("SUCCESS");
        co_return;
    });

    server.set_router(std::move(router));

    // Simulate request passing through server
    Request req;
    req.set_method(Method::POST);
    req.set_path("/items");
    Response res;

    Context ctx(req, res, &server.services());
    auto match = server.router().match(req);
    TEST_CHECK(match.route_found);
    TEST_CHECK(match.handler != nullptr);

    auto task = (*match.handler)(ctx);
    task.resume();

    TEST_CHECK(handler_called);
    TEST_CHECK(res.status() == StatusCode::Ok);
    TEST_CHECK(res.body() == "SUCCESS");

    std::cout << " -> SQLite in-memory test passed successfully!\n";
}

void test_postgres_config() {
    std::cout << "[TEST 2] Testing Server.provide with live PostgreSQL 17...\n";

    SqlConfig config{
        .dialect = DatabaseDialect::PostgreSQL,
        .host = "127.0.0.1",
        .port = 5432,
        .database = "aegon_test",
        .user = "postgres",
        .password = "postgres",
        .pool_per_core = 4
    };
    auto pool = drivers::create_postgres_pool(config.to_conninfo(), config.pool_per_core);
    auto client = std::make_shared<SqlDatabaseClient>(*pool);

    Server server;
    server.provide(client);

    TEST_CHECK(server.services().has<SqlDatabaseClient>());

    Router router;
    bool handler_called = false;

    router.get("/pg-test", [&](Context& ctx) -> Task<void> {
        handler_called = true;
        TEST_CHECK(ctx.has_service<SqlDatabaseClient>());
        auto& db = ctx.service<SqlDatabaseClient>();

        // Recreate table in PostgreSQL
        co_await db.execute("DROP TABLE IF EXISTS server_items CASCADE;");
        auto ddl = generate_ddl<ServerItem>(DatabaseDialect::PostgreSQL);
        co_await db.execute(ddl);

        // Transaction block
        co_await db.transaction([](Transaction& tx) -> Task<void> {
            ServerItem it1{.id = 0, .name = "Valyrian Blade", .quantity = 1};
            ServerItem it2{.id = 0, .name = "Obsidian Arrow", .quantity = 50};
            co_await tx.insert(it1);
            co_await tx.insert(it2);
            co_return;
        });

        auto items = co_await db.fetch_all(
            db.from<ServerItem>().where(&ServerItem::quantity, Op::Gt, 5)
        );
        TEST_CHECK(items.size() == 1);
        TEST_CHECK(items[0].id > 0);
        TEST_CHECK(items[0].name == "Obsidian Arrow");
        TEST_CHECK(items[0].quantity == 50);

        ctx.res().status(StatusCode::Ok).text("PG_OK");
        co_return;
    });

    server.set_router(std::move(router));

    Request req;
    req.set_method(Method::GET);
    req.set_path("/pg-test");
    Response res;

    Context ctx(req, res, &server.services());
    auto match = server.router().match(req);
    TEST_CHECK(match.route_found);

    auto task = (*match.handler)(ctx);
    task.resume();

    TEST_CHECK(handler_called);
    TEST_CHECK(res.status() == StatusCode::Ok);
    TEST_CHECK(res.body() == "PG_OK");

    std::cout << " -> Live PostgreSQL 17 test passed successfully!\n";
}

void test_server_move_and_reconfiguration() {
    std::cout << "[TEST 3] Testing Server move operations with ServiceRegistry...\n";

    auto pool = drivers::create_sqlite_pool(":memory:", 2);
    auto client = std::make_shared<SqlDatabaseClient>(*pool);

    Server server;
    server.provide(client).listen(9090).enable_http3(false);

    TEST_CHECK(server.port() == 9090);
    TEST_CHECK(!server.is_http3_enabled());
    TEST_CHECK(server.services().has<SqlDatabaseClient>());

    // Move constructor
    Server moved_server = std::move(server);
    TEST_CHECK(moved_server.port() == 9090);
    TEST_CHECK(moved_server.services().has<SqlDatabaseClient>());

    // Move assignment
    Server target_server;
    target_server = std::move(moved_server);
    TEST_CHECK(target_server.port() == 9090);
    TEST_CHECK(target_server.services().has<SqlDatabaseClient>());

    std::cout << " -> Server move operations passed successfully!\n";
}

void test_external_sql_client_injection() {
    std::cout << "[TEST 4] Testing external SqlDatabaseClient injection via provide()...\n";

    auto ext_pool = drivers::create_sqlite_pool(":memory:", 2);
    auto ext_client = std::make_shared<SqlDatabaseClient>(*ext_pool);

    Server server;
    server.provide(ext_client);

    TEST_CHECK(server.services().has<SqlDatabaseClient>());
    TEST_CHECK(server.services().get<SqlDatabaseClient>() == ext_client.get());

    std::cout << " -> External client injection passed successfully!\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "      Aegon Server SQL Configuration Test Suite        \n";
    std::cout << "========================================================\n";

    test_sqlite_in_memory_config();
    test_postgres_config();
    test_server_move_and_reconfiguration();
    test_external_sql_client_injection();

    std::cout << "========================================================\n";
    std::cout << "  ALL SERVER SQL CONFIGURATION TESTS PASSED!\n";
    std::cout << "========================================================\n";
    return 0;
}
