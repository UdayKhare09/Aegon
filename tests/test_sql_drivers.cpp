#include "data/orm/sql/Sql.h"
#include "data/orm/sql/drivers/SqliteDriver.h"
#include "data/orm/sql/drivers/PostgresDriver.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <optional>

#define TEST_CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace aegon::core;
using namespace aegon::data;
using namespace aegon::data::orm::sql;
using namespace aegon::data::orm::sql::drivers;

// Test Entities
struct User {
    UUID id;
    std::string email;
    std::string username;
    Decimal128 balance;

    static auto schema() {
        return table<User>("users")
            .id(&User::id)
            .column(&User::email, "email").unique()
            .column(&User::username, "username")
            .column(&User::balance, "balance");
    }
};

struct Order {
    UUID id;
    UUID user_id;
    Decimal128 total;
    std::string status;

    static auto schema() {
        return table<Order>("orders")
            .id(&Order::id)
            .column(&Order::user_id, "user_id").references<User>(&User::id).on_delete_cascade()
            .column(&Order::total, "total")
            .column(&Order::status, "status");
    }
};

Task<void> test_sqlite_live_engine() {
    std::cout << "[Test 1] Testing Live SQLite Engine (DDL, Auto-Mapping, Option 4 Transactions)...\n";

    // 1. Create SQLite in-memory pool
    auto pool = create_sqlite_pool(":memory:", 4);
    SqlDatabaseClient db(*pool);

    // 2. Run DDL migrations
    co_await db.execute(generate_ddl<User>(DatabaseDialect::SQLite), {});
    co_await db.execute(generate_ddl<Order>(DatabaseDialect::SQLite), {});

    User user{
        .id = UUID::from_string("550e8400-e29b-41d4-a716-446655440000").value(),
        .email = "uday@aegon.dev",
        .username = "uday",
        .balance = Decimal128::from_string("1000.0000").value()
    };

    // 3. Direct entity insert with parameter binding
    co_await db.insert(user);

    // 4. Direct find by primary key with entity auto-hydration
    auto found = co_await db.find_by_id<User>(user.id);
    TEST_CHECK(found.has_value());
    TEST_CHECK(found->id.to_string() == "550e8400-e29b-41d4-a716-446655440000");
    TEST_CHECK(found->email == "uday@aegon.dev");
    TEST_CHECK(found->username == "uday");
    TEST_CHECK(found->balance.to_string() == "1000.0000");

    // 5. Option 4 Transaction: atomic update + order creation
    Order order{
        .id = UUID::from_string("6ba7b810-9dad-11d1-80b4-00c04fd430c8").value(),
        .user_id = user.id,
        .total = Decimal128::from_string("150.0000").value(),
        .status = "PAID"
    };

    co_await db.transaction([&](Transaction& tx) -> Task<void> {
        user.balance = Decimal128::from_string("850.0000").value();
        co_await tx.update_entity(user);
        co_await tx.insert(order);
    });

    // Verify transaction commit
    auto user_after_tx = co_await db.find_by_id<User>(user.id);
    TEST_CHECK(user_after_tx.has_value());
    TEST_CHECK(user_after_tx->balance.to_string() == "850.0000");

    auto orders = co_await db.fetch_all(from<Order>().where(&Order::user_id, Op::Eq, user.id));
    TEST_CHECK(orders.size() == 1);
    TEST_CHECK(orders[0].id.to_string() == "6ba7b810-9dad-11d1-80b4-00c04fd430c8");
    TEST_CHECK(orders[0].total.to_string() == "150.0000");
    TEST_CHECK(orders[0].status == "PAID");

    // 6. Option 4 Transaction: rollback on simulated exception
    bool rollback_caught = false;
    try {
        co_await db.transaction([&](Transaction& tx) -> Task<void> {
            Order bad_order{
                .id = UUID::from_string("7ba7b810-9dad-11d1-80b4-00c04fd430c9").value(),
                .user_id = user.id,
                .total = Decimal128::from_string("999.0000").value(),
                .status = "UNPAID"
            };
            co_await tx.insert(bad_order);

            // Simulate business error
            throw std::runtime_error("Simulated transaction failure");
        });
    } catch (const std::runtime_error& e) {
        rollback_caught = true;
        TEST_CHECK(std::string(e.what()) == "Simulated transaction failure");
    }

    TEST_CHECK(rollback_caught);

    // Verify bad_order was rolled back
    auto bad_order_search = co_await db.find_by_id<Order>(UUID::from_string("7ba7b810-9dad-11d1-80b4-00c04fd430c9").value());
    TEST_CHECK(!bad_order_search.has_value());

    // 7. Delete with Foreign Key Cascade
    bool deleted = co_await db.delete_by_id<User>(user.id);
    TEST_CHECK(deleted);

    auto user_deleted = co_await db.find_by_id<User>(user.id);
    TEST_CHECK(!user_deleted.has_value());

    auto orders_cascaded = co_await db.fetch_all(from<Order>());
    TEST_CHECK(orders_cascaded.empty()); // Order automatically cascade deleted

    std::cout << "  -> PASS: Live SQLite engine full CRUD, transactions, auto-commit, rollback, and cascade verified!\n";
}

void test_postgres_driver_specs() {
    std::cout << "[Test 2] Testing PostgreSQL Driver Specifications...\n";

    auto pool = create_postgres_pool("host=127.0.0.1 dbname=test", 4);
    TEST_CHECK(pool != nullptr);

    std::cout << "  -> PASS: Postgres pool and connection types compiled and configured.\n";
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "     AEGON C++26 SQL DATABASE DRIVERS TEST SUITE       \n";
    std::cout << "=======================================================\n\n";

    auto run_task = [](Task<void> t) {
        t.resume();
        assert(t.is_ready());
        t.result();
    };

    run_task(test_sqlite_live_engine());
    test_postgres_driver_specs();

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL SQL DATABASE DRIVER TESTS PASSED! <<<       \n";
    std::cout << "=======================================================\n\n";
    return 0;
}
