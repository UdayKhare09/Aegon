#include "data/orm/sql/Sql.h"
#include "data/orm/sql/drivers/MockDriver.h"
#include "data/orm/sql/OptimisticLockException.h"
#include "data/cache/CacheBackend.h"
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#define TEST_CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace aegon;
using namespace aegon::core;
using namespace aegon::data::orm::sql;
using namespace aegon::data::orm::sql::drivers;
using namespace aegon::data::cache;

// --- Test Entities ---
struct Product {
    int64_t id{0};
    std::string name;
    int64_t price{0};
    int64_t version{1};

    static auto schema() {
        return table<Product>("products")
            .id(&Product::id, "id")
            .column(&Product::name, "name")
            .column(&Product::price, "price")
            .version(&Product::version, "version");
    }
};

struct Ticket {
    int64_t id{0};
    std::string status;
    std::string priority;

    static auto schema() {
        return table<Ticket>("tickets")
            .id(&Ticket::id, "id")
            .column(&Ticket::status, "status")
            .column(&Ticket::priority, "priority")
            .cache_by_id()
            .invalidation_mode(InvalidationMode::PredicateAware);
    }
};

// --- In-Memory Cache for Predicate Testing ---
class MockCacheBackend : public CacheBackend {
public:
    std::unordered_map<std::string, std::string> store_;
    std::vector<std::string> incremented_keys_;

    core::Task<std::optional<std::string>> get(std::string_view key) override {
        auto it = store_.find(std::string(key));
        if (it != store_.end()) co_return it->second;
        co_return std::nullopt;
    }

    core::Task<std::vector<std::optional<std::string>>> mget(const std::vector<std::string>& keys) override {
        std::vector<std::optional<std::string>> res;
        for (const auto& k : keys) {
            auto it = store_.find(k);
            if (it != store_.end()) res.push_back(it->second);
            else res.push_back(std::nullopt);
        }
        co_return res;
    }

    core::Task<bool> set(std::string_view key, std::string_view val, std::optional<std::chrono::seconds>) override {
        store_[std::string(key)] = std::string(val);
        co_return true;
    }

    core::Task<bool> del(std::string_view key) override {
        store_.erase(std::string(key));
        co_return true;
    }

    core::Task<int64_t> del_many(const std::vector<std::string>& keys) override {
        int64_t count = 0;
        for (const auto& k : keys) {
            if (store_.erase(k)) count++;
        }
        co_return count;
    }

    core::Task<int64_t> incr(std::string_view key) override {
        std::string k(key);
        incremented_keys_.push_back(k);
        int64_t val = 0;
        if (store_.contains(k)) {
            val = std::stoll(store_[k]);
        }
        val++;
        store_[k] = std::to_string(val);
        co_return val;
    }
};

struct ConnState {
    std::unique_ptr<MockConnection> conn;
};

// =========================================================================
// Test 1: Optimistic Concurrency Control (.version)
// =========================================================================
Task<void> test_optimistic_concurrency() {
    std::cout << "[Test 1] Testing Optimistic Concurrency Control (.version)...\n";

    auto state = std::make_shared<ConnState>();
    state->conn = std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
    MockConnection* mock_conn = state->conn.get();
    PerCoreConnectionPool pool([state]() -> std::unique_ptr<Connection> {
        if (state->conn) return std::move(state->conn);
        return std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
    });

    SqlDatabaseClient db(pool);

    Product prod{
        .id = 42,
        .name = "Ergonomic Keyboard",
        .price = 150,
        .version = 1
    };

    // 1. Normal Update - succeeds
    mock_conn->next_execute_result_ = 1;
    size_t affected = co_await db.update_entity(prod);
    TEST_CHECK(affected == 1);
    TEST_CHECK(prod.version == 2); // Version bumped in memory

    // Verify generated SQL includes version in SET and WHERE
    TEST_CHECK(!mock_conn->executed_sqls_.empty());
    const auto& last_sql = mock_conn->executed_sqls_.back();
    TEST_CHECK(last_sql.find("\"version\" = $") != std::string::npos);
    TEST_CHECK(last_sql.find("WHERE") != std::string::npos);
    std::cout << "  -> Update verified CAS SQL: " << last_sql << "\n";
    std::cout << "  -> Entity version incremented to: " << prod.version << "\n";

    // 2. Concurrent Update Conflict (rows_affected == 0) -> throws OptimisticLockException
    mock_conn->next_execute_result_ = 0; // Simulate 0 rows updated because version changed
    bool caught = false;
    try {
        co_await db.update_entity(prod);
    } catch (const OptimisticLockException& e) {
        caught = true;
        std::cout << "  -> Successfully caught OptimisticLockException: " << e.what() << "\n";
    }
    TEST_CHECK(caught);
}

// =========================================================================
// Test 2: PredicateAware Query Cache Invalidation
// =========================================================================
Task<void> test_predicate_aware_invalidation() {
    std::cout << "\n[Test 2] Testing PredicateAware Query Cache Invalidation...\n";

    auto state = std::make_shared<ConnState>();
    state->conn = std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
    PerCoreConnectionPool pool([state]() -> std::unique_ptr<Connection> {
        if (state->conn) return std::move(state->conn);
        return std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
    });

    SqlDatabaseClient db(pool);
    auto cache = std::make_shared<MockCacheBackend>();
    db.set_cache(cache);

    // 1. Verify predicate extraction on SelectBuilder
    auto query_open = db.from<Ticket>().where(&Ticket::status, Op::Eq, "open").cached(InvalidationMode::PredicateAware);
    auto preds = query_open.extract_equality_predicates();
    TEST_CHECK(preds.size() == 1);
    TEST_CHECK(preds[0].first == "status" && preds[0].second == "open");
    std::cout << "  -> Extracted equality predicate: " << preds[0].first << " = " << preds[0].second << "\n";

    // 2. Insert a ticket with status="open"
    Ticket t1{.id = 101, .status = "open", .priority = "high"};
    co_await db.insert(t1);

    // Verify predicate epoch was incremented for status:open
    bool found_open_pred = false;
    for (const auto& k : cache->incremented_keys_) {
        if (k == "tickets:pred:status:open:epoch") {
            found_open_pred = true;
            break;
        }
    }
    TEST_CHECK(found_open_pred);
    std::cout << "  -> Predicate epoch bumped: tickets:pred:status:open:epoch\n";

    // Verify table-wide epoch tickets:epoch was NOT bumped
    for (const auto& k : cache->incremented_keys_) {
        TEST_CHECK(k != "tickets:epoch");
    }
    std::cout << "  -> Table-wide tickets:epoch was NOT bumped (fine-grained scoping verified)\n";

    // 3. Update ticket with status="open"
    cache->incremented_keys_.clear();
    t1.priority = "critical";
    co_await db.update_entity(t1);

    found_open_pred = false;
    for (const auto& k : cache->incremented_keys_) {
        if (k == "tickets:pred:status:open:epoch") {
            found_open_pred = true;
            break;
        }
    }
    TEST_CHECK(found_open_pred);
    // Ensure tickets:pred:status:closed:epoch was untouched
    for (const auto& k : cache->incremented_keys_) {
        TEST_CHECK(k != "tickets:pred:status:closed:epoch");
    }
    std::cout << "  -> Mutation on open ticket bumped tickets:pred:status:open:epoch but left closed tickets untouched\n";
}

// =========================================================================
// Test 3: Read Replica Splitting & Round-Robin Load Balancing
// =========================================================================
Task<void> test_read_replica_splitting() {
    std::cout << "\n[Test 3] Testing Primary / Replica Read Splitting & Round-Robin...\n";

    // 1. Primary pool
    auto primary_state = std::make_shared<ConnState>();
    primary_state->conn = std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
    MockConnection* primary_conn = primary_state->conn.get();
    PerCoreConnectionPool primary_pool([primary_state]() -> std::unique_ptr<Connection> {
        if (primary_state->conn) return std::move(primary_state->conn);
        return std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
    });

    // 2. Replica pool 1
    auto replica1_state = std::make_shared<ConnState>();
    replica1_state->conn = std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
    MockConnection* replica1_conn = replica1_state->conn.get();
    PerCoreConnectionPool replica1_pool([replica1_state]() -> std::unique_ptr<Connection> {
        if (replica1_state->conn) return std::move(replica1_state->conn);
        return std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
    });

    // 3. Replica pool 2
    auto replica2_state = std::make_shared<ConnState>();
    replica2_state->conn = std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
    MockConnection* replica2_conn = replica2_state->conn.get();
    PerCoreConnectionPool replica2_pool([replica2_state]() -> std::unique_ptr<Connection> {
        if (replica2_state->conn) return std::move(replica2_state->conn);
        return std::make_unique<MockConnection>(DatabaseDialect::PostgreSQL);
    });

    // --- Part A: Zero-replica fallback ---
    {
        SqlDatabaseClient single_db(primary_pool);
        size_t initial_primary = primary_conn->executed_sqls_.size();
        co_await single_db.find_by_id<Product>(1);
        TEST_CHECK(primary_conn->executed_sqls_.size() == initial_primary + 1);
        std::cout << "  -> Fallback verified: reads with 0 replicas route directly to PRIMARY pool\n";
    }

    // --- Part B: Multi-replica Client (1 Primary + 2 Replicas) ---
    SqlDatabaseClient db(primary_pool, {replica1_pool, replica2_pool});
    Product prod{.id = 1, .name = "Widget", .price = 10, .version = 1};

    // 1. All Mutation Operations must strictly hit PRIMARY pool
    size_t prim_before_mutations = primary_conn->executed_sqls_.size();
    size_t rep1_before = replica1_conn->executed_sqls_.size();
    size_t rep2_before = replica2_conn->executed_sqls_.size();

    co_await db.insert(prod);
    prod.price = 20;
    co_await db.update_entity(prod);
    co_await db.delete_by_id<Product>(prod.id);
    co_await db.execute("VACUUM ANALYZE \"products\";");

    TEST_CHECK(primary_conn->executed_sqls_.size() == prim_before_mutations + 4);
    TEST_CHECK(replica1_conn->executed_sqls_.size() == rep1_before);
    TEST_CHECK(replica2_conn->executed_sqls_.size() == rep2_before);
    std::cout << "  -> All mutations (insert, update, delete, execute) strictly routed to PRIMARY pool\n";

    // 2. Read Operations with Round-Robin across Replica 1 and Replica 2
    // Read 1 -> Replica 1
    co_await db.find_by_id<Product>(1);
    TEST_CHECK(replica1_conn->executed_sqls_.size() == rep1_before + 1);
    TEST_CHECK(replica2_conn->executed_sqls_.size() == rep2_before);

    // Read 2 -> Replica 2
    co_await db.fetch_all(db.from<Product>());
    TEST_CHECK(replica1_conn->executed_sqls_.size() == rep1_before + 1);
    TEST_CHECK(replica2_conn->executed_sqls_.size() == rep2_before + 1);

    // Read 3 -> Replica 1
    co_await db.count(db.from<Product>());
    TEST_CHECK(replica1_conn->executed_sqls_.size() == rep1_before + 2);
    TEST_CHECK(replica2_conn->executed_sqls_.size() == rep2_before + 1);

    // Read 4 -> Replica 2
    co_await db.fetch_one(db.from<Product>());
    TEST_CHECK(replica1_conn->executed_sqls_.size() == rep1_before + 2);
    TEST_CHECK(replica2_conn->executed_sqls_.size() == rep2_before + 2);

    // Read 5 -> Replica 1
    co_await db.avg(db.from<Product>(), &Product::price);
    TEST_CHECK(replica1_conn->executed_sqls_.size() == rep1_before + 3);
    TEST_CHECK(replica2_conn->executed_sqls_.size() == rep2_before + 2);

    // Read 6 -> Replica 2
    co_await db.find_by_unique(&Product::name, "Widget");
    TEST_CHECK(replica1_conn->executed_sqls_.size() == rep1_before + 3);
    TEST_CHECK(replica2_conn->executed_sqls_.size() == rep2_before + 3);

    std::cout << "  -> Round-robin load balancing verified across 2 replicas (3 queries to Rep1, 3 queries to Rep2)\n";

    // 3. Transactions must strictly pin ALL operations (reads and writes) to PRIMARY
    size_t prim_before_tx = primary_conn->executed_sqls_.size();
    rep1_before = replica1_conn->executed_sqls_.size();
    rep2_before = replica2_conn->executed_sqls_.size();

    co_await db.transaction([&](Transaction& tx) -> Task<void> {
        co_await tx.find_by_id<Product>(1);
        co_await tx.fetch_all(db.from<Product>());
        co_await tx.insert(prod);
        co_await tx.update_entity(prod);
    });

    TEST_CHECK(primary_conn->executed_sqls_.size() > prim_before_tx);
    TEST_CHECK(replica1_conn->executed_sqls_.size() == rep1_before); // Rep1 untouched
    TEST_CHECK(replica2_conn->executed_sqls_.size() == rep2_before); // Rep2 untouched
    std::cout << "  -> Transaction pinning verified: both reads and writes executed strictly on PRIMARY\n";
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "   AEGON ADVANCED ORM CAPABILITIES TEST SUITE          \n";
    std::cout << "=======================================================\n\n";

    auto run_task = [](Task<void> t) {
        t.resume();
        TEST_CHECK(t.is_ready());
        t.result();
    };

    run_task(test_optimistic_concurrency());
    run_task(test_predicate_aware_invalidation());
    run_task(test_read_replica_splitting());

    std::cout << "\n=======================================================\n";
    std::cout << " >>> ALL ADVANCED ORM TESTS PASSED! <<<\n";
    std::cout << "=======================================================\n\n";
    return 0;
}
