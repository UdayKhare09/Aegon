#include "data/orm/sql/Sql.h"
#include "data/orm/sql/drivers/MockDriver.h"
#include "http/Context.h"
#include "http/Request.h"
#include "http/Response.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <memory>

#define TEST_CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace aegon::core;
using namespace aegon::http;
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
            .column(&User::email, "email")
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
            .column(&Order::user_id, "user_id").references<User>(&User::id)
            .column(&Order::total, "total")
            .column(&Order::status, "status");
    }
};

// =========================================================================
// Test 1: Successful Transaction Commit (Option 4)
// =========================================================================
Task<void> test_transaction_commit() {
    std::cout << "[Test 1] Testing Option 4 Transaction & Unit of Work (Successful Commit)...\n";

    MockConnection* mock_ptr = nullptr;
    PerCoreConnectionPool pool([&]() -> std::unique_ptr<Connection> {
        auto conn = std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
        mock_ptr = conn.get();
        return conn;
    });

    SqlDatabaseClient db(pool);

    User user{
        .id = UUID::from_string("a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11").value(),
        .email = "uday@aegon.dev",
        .username = "uday",
        .balance = Decimal128::from_string("1000.0000").value()
    };

    Order order{
        .id = UUID::from_string("b1ffcd00-0c1c-2ef9-cc7e-7cc0ce491b22").value(),
        .user_id = user.id,
        .total = Decimal128::from_string("150.0000").value(),
        .status = "PENDING"
    };

    // Execute atomic transaction
    co_await db.transaction([&](Transaction& tx) -> Task<void> {
        co_await tx.insert(user);
        co_await tx.insert(order);

        // Deduct balance
        user.balance = Decimal128::from_string("850.0000").value();
        co_await tx.update_entity(user);
    });

    TEST_CHECK(mock_ptr != nullptr);
    TEST_CHECK(mock_ptr->begin_count_ == 1);
    TEST_CHECK(mock_ptr->commit_count_ == 1);
    TEST_CHECK(mock_ptr->rollback_count_ == 0);
    TEST_CHECK(!mock_ptr->in_transaction_);
    TEST_CHECK(mock_ptr->executed_sqls_.size() == 3);
    TEST_CHECK(mock_ptr->executed_sqls_[0].find("INSERT INTO \"users\"") != std::string::npos);
    TEST_CHECK(mock_ptr->executed_sqls_[1].find("INSERT INTO \"orders\"") != std::string::npos);
    TEST_CHECK(mock_ptr->executed_sqls_[2].find("UPDATE \"users\" SET") != std::string::npos);

    std::cout << "  -> PASS: Atomic transaction committed cleanly with 3 entity mutations.\n";
}

// =========================================================================
// Test 2: Automatic Transaction Rollback on Error
// =========================================================================
Task<void> test_transaction_rollback() {
    std::cout << "[Test 2] Testing Automatic Transaction Rollback on Exception...\n";

    MockConnection* mock_ptr = nullptr;
    PerCoreConnectionPool pool([&]() -> std::unique_ptr<Connection> {
        auto conn = std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
        mock_ptr = conn.get();
        return conn;
    });

    SqlDatabaseClient db(pool);

    User user{
        .id = UUID::from_string("a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11").value(),
        .email = "rollback@aegon.dev",
        .username = "rollback_user",
        .balance = Decimal128::from_string("50.0000").value()
    };

    bool exception_caught = false;
    try {
        co_await db.transaction([&](Transaction& tx) -> Task<void> {
            co_await tx.insert(user);
            // Simulate business logic validation failure (insufficient funds)
            throw std::runtime_error("Insufficient funds for transaction");
        });
    } catch (const std::runtime_error& e) {
        exception_caught = true;
        TEST_CHECK(std::string(e.what()) == "Insufficient funds for transaction");
    }

    TEST_CHECK(exception_caught);
    TEST_CHECK(mock_ptr != nullptr);
    TEST_CHECK(mock_ptr->begin_count_ == 1);
    TEST_CHECK(mock_ptr->commit_count_ == 0);
    TEST_CHECK(mock_ptr->rollback_count_ == 1);
    TEST_CHECK(!mock_ptr->in_transaction_);

    std::cout << "  -> PASS: Exception inside transaction automatically issued ROLLBACK.\n";
}

// =========================================================================
// Test 3: Multi-Database Context Namespace (`ctx.db.sql`)
// =========================================================================
Task<void> test_context_db_sql_namespace() {
    std::cout << "[Test 3] Testing Context multi-database namespace (ctx.db.sql)...\n";

    MockConnection* mock_ptr = nullptr;
    PerCoreConnectionPool pool([&]() -> std::unique_ptr<Connection> {
        auto conn = std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
        mock_ptr = conn.get();
        return conn;
    });

    SqlDatabaseClient db(pool);

    Request req;
    Response res;
    Context ctx(req, res, nullptr, &db);

    TEST_CHECK(ctx.db.sql.is_configured());

    User user{
        .id = UUID::from_string("550e8400-e29b-41d4-a716-446655440000").value(),
        .email = "ctx_user@aegon.dev",
        .username = "ctx_user",
        .balance = Decimal128::from_string("300.0000").value()
    };

    // Option 4 invocation directly via ctx.db.sql:
    co_await ctx.db.sql.transaction([&](Transaction& tx) -> Task<void> {
        co_await tx.insert(user);
    });

    TEST_CHECK(mock_ptr != nullptr);
    TEST_CHECK(mock_ptr->commit_count_ == 1);

    // Test unconfigured database check
    Request req2;
    Response res2;
    Context ctx_no_db(req2, res2);
    TEST_CHECK(!ctx_no_db.db.sql.is_configured());

    bool threw_unconfigured = false;
    try {
        ctx_no_db.db.sql.from<User>();
    } catch (const std::runtime_error& e) {
        threw_unconfigured = true;
    }
    TEST_CHECK(threw_unconfigured);

    std::cout << "  -> PASS: ctx.db.sql namespace works seamlessly in Context.\n";
}

// =========================================================================
// Test 4: Direct Non-Transactional Operations
// =========================================================================
Task<void> test_direct_operations() {
    std::cout << "[Test 4] Testing Direct Operations on SqlDatabaseClient...\n";

    MockConnection* mock_ptr = nullptr;
    PerCoreConnectionPool pool([&]() -> std::unique_ptr<Connection> {
        auto conn = std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
        mock_ptr = conn.get();
        return conn;
    });

    SqlDatabaseClient db(pool);

    User user{
        .id = UUID::from_string("a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11").value(),
        .email = "direct@aegon.dev",
        .username = "direct_user",
        .balance = Decimal128::from_string("100.0000").value()
    };

    // 1. Direct Insert
    co_await db.insert(user);
    TEST_CHECK(mock_ptr->executed_sqls_.size() == 1);
    TEST_CHECK(mock_ptr->executed_sqls_[0].find("INSERT INTO \"users\"") != std::string::npos);

    // 2. Direct Delete
    co_await db.delete_by_id<User>(user.id);
    TEST_CHECK(mock_ptr->executed_sqls_.size() == 2);
    TEST_CHECK(mock_ptr->executed_sqls_[1].find("DELETE FROM \"users\" WHERE \"id\" = $1") != std::string::npos);

    // 3. Direct Find By ID (with mock row return)
    MockRowView row;
    row.add_value(user.id.to_string());
    row.add_value(user.email);
    row.add_value(user.username);
    row.add_value(user.balance.to_string());
    mock_ptr->mock_results_.push_back(row);

    auto found = co_await db.find_by_id<User>(user.id);
    TEST_CHECK(found.has_value());
    TEST_CHECK(found->email == "direct@aegon.dev");
    TEST_CHECK(found->balance.to_string() == "100.0000");

    std::cout << "  -> PASS: Direct CRUD conveniences work without manual transaction boilerplate.\n";
}

// =========================================================================
// Test 5: Per-Core Connection Pool Reuse
// =========================================================================
Task<void> test_pool_connection_reuse() {
    std::cout << "[Test 5] Testing Per-Core Lock-Free Connection Pool Reuse...\n";

    int connections_created = 0;
    PerCoreConnectionPool pool([&]() -> std::unique_ptr<Connection> {
        ++connections_created;
        return std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
    }, 4);

    TEST_CHECK(pool.idle_count() == 0);

    {
        auto guard1 = pool.acquire();
        TEST_CHECK(connections_created == 1);
        TEST_CHECK(pool.idle_count() == 0);
    } // guard1 released back to pool

    TEST_CHECK(pool.idle_count() == 1);

    {
        auto guard2 = pool.acquire();
        // Should reuse the existing connection!
        TEST_CHECK(connections_created == 1);
        TEST_CHECK(pool.idle_count() == 0);
    } // guard2 released back to pool

    TEST_CHECK(pool.idle_count() == 1);

    std::cout << "  -> PASS: Connection acquired, released, and reused without reallocating.\n";
    co_return;
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "   AEGON C++26 SQL ORM TRANSACTION & POOL TEST SUITE   \n";
    std::cout << "=======================================================\n\n";

    // Run coroutine tasks synchronously using Aegon Task test harness
    auto run_task = [](Task<void> t) {
        t.resume();
        assert(t.is_ready());
        t.result();
    };

    run_task(test_transaction_commit());
    run_task(test_transaction_rollback());
    run_task(test_context_db_sql_namespace());
    run_task(test_direct_operations());
    run_task(test_pool_connection_reuse());

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL SQL TRANSACTION & POOL TESTS PASSED! <<<    \n";
    std::cout << "=======================================================\n\n";
    return 0;
}
