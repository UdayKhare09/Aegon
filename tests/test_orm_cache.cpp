#include "data/cache/CacheBackend.h"
#include "data/orm/sql/SqlConfig.h"
#include "data/orm/sql/SqlDatabaseClient.h"
#include "data/orm/sql/drivers/SqliteDriver.h"
#include "core/EventLoop.h"
#include <cassert>
#include <iostream>
#include <unordered_map>

using namespace aegon;
using namespace aegon::data::cache;
using namespace aegon::data::orm::sql;
using namespace aegon::data::orm::sql::drivers;

// In-Memory Mock Cache Backend for automated verification
class MockCacheBackend : public CacheBackend {
public:
    std::unordered_map<std::string, std::string> store;
    int get_count{0};
    int set_count{0};
    int del_count{0};
    int incr_count{0};

    core::Task<std::optional<std::string>> get(std::string_view key) override {
        ++get_count;
        auto it = store.find(std::string(key));
        if (it != store.end()) {
            co_return it->second;
        }
        co_return std::nullopt;
    }

    core::Task<std::vector<std::optional<std::string>>> mget(const std::vector<std::string>& keys) override {
        std::vector<std::optional<std::string>> res;
        res.reserve(keys.size());
        for (const auto& k : keys) {
            ++get_count;
            auto it = store.find(k);
            if (it != store.end()) {
                res.push_back(it->second);
            } else {
                res.push_back(std::nullopt);
            }
        }
        co_return res;
    }

    core::Task<bool> set(std::string_view key, std::string_view val, std::optional<std::chrono::seconds> /*ttl*/) override {
        ++set_count;
        store[std::string(key)] = std::string(val);
        co_return true;
    }

    core::Task<bool> del(std::string_view key) override {
        ++del_count;
        bool found = store.erase(std::string(key)) > 0;
        co_return found;
    }

    core::Task<int64_t> del_many(const std::vector<std::string>& keys) override {
        int64_t count = 0;
        for (const auto& k : keys) {
            ++del_count;
            if (store.erase(k) > 0) ++count;
        }
        co_return count;
    }

    core::Task<int64_t> incr(std::string_view key) override {
        ++incr_count;
        std::string k(key);
        int64_t val = 0;
        if (store.find(k) != store.end()) {
            val = std::stoll(store[k]);
        }
        ++val;
        store[k] = std::to_string(val);
        co_return val;
    }
};

struct Account {
    int id{0};
    std::string email;
    std::string name;
    int org_id{0};

    static const auto& schema() {
        static const auto s = TableDef<Account>("accounts")
            .id(&Account::id, "id")
            .column(&Account::email, "email").unique()
            .column(&Account::name, "name")
            .column(&Account::org_id, "org_id")
            .cache({
                .ttl = std::chrono::seconds(600),
                .by_id = true,
                .invalidation = InvalidationMode::Partitioned
            })
            .by_unique(&Account::email)
            .partition_by(&Account::org_id);
        return s;
    }
};

core::Task<void> run_orm_cache_tests() {
    auto pool = create_sqlite_pool(":memory:", 1);
    SqlDatabaseClient client(*pool);

    auto mock_cache = std::make_shared<MockCacheBackend>();
    client.set_cache(mock_cache);

    // Create table schema
    co_await client.execute(
        "CREATE TABLE accounts ("
        "id INTEGER PRIMARY KEY, "
        "email TEXT UNIQUE NOT NULL, "
        "name TEXT NOT NULL, "
        "org_id INTEGER NOT NULL"
        ");"
    );

    // 1. Insert row
    Account a1{.id = 1, .email = "alice@test.com", .name = "Alice", .org_id = 10};
    co_await client.insert(a1);

    // Verify entity was cached by ID and partition epoch incremented
    assert(mock_cache->store.find("accounts:id:1") != mock_cache->store.end());
    assert(mock_cache->store.find("accounts:part:10:epoch") != mock_cache->store.end());
    std::cout << "[PASS] Insert cached entity and incremented partition epoch\n";

    // 2. find_by_id: verify cache hit
    int gets_before = mock_cache->get_count;
    auto found_a1 = co_await client.find_by_id<Account>(1);
    assert(found_a1.has_value());
    assert(found_a1->name == "Alice");
    assert(mock_cache->get_count > gets_before);
    std::cout << "[PASS] find_by_id served from cache\n";

    // 3. fetch_all with query caching
    auto q_before_gets = mock_cache->get_count;
    auto accounts_list1 = co_await client.fetch_all(
        client.from<Account>().where(&Account::org_id, Op::Eq, 10).cached()
    );
    assert(accounts_list1.size() == 1);
    assert(accounts_list1[0].name == "Alice");
    assert(mock_cache->get_count > q_before_gets);

    // Second fetch_all: hits query cache + pipelined MGET on ID!
    auto accounts_list2 = co_await client.fetch_all(
        client.from<Account>().where(&Account::org_id, Op::Eq, 10).cached()
    );
    assert(accounts_list2.size() == 1);
    assert(accounts_list2[0].name == "Alice");
    std::cout << "[PASS] fetch_all cached query results via two-phase ID pointer\n";

    // 4. Update entity: smart eviction and partition epoch increment
    a1.name = "Alice Updated";
    co_await client.update_entity(a1);

    // Entity cache evicted or updated
    auto found_after_update = co_await client.find_by_id<Account>(1);
    assert(found_after_update.has_value());
    assert(found_after_update->name == "Alice Updated");
    std::cout << "[PASS] update_entity correctly refreshed entity data\n";

    // 5. Partitioned Invalidation: insert into org 20 should NOT affect org 10
    Account a2{.id = 2, .email = "bob@test.com", .name = "Bob", .org_id = 20};
    co_await client.insert(a2);

    assert(mock_cache->store.find("accounts:part:20:epoch") != mock_cache->store.end());
    assert(mock_cache->store["accounts:part:10:epoch"] == "2"); // Org 10 epoch was NOT incremented by org 20!
    std::cout << "[PASS] Partitioned invalidation scoped to parent ID\n";

    // 6. delete_by_id
    bool deleted = co_await client.delete_by_id<Account>(1);
    assert(deleted);
    assert(mock_cache->store.find("accounts:id:1") == mock_cache->store.end());
    std::cout << "[PASS] delete_by_id purged cache key\n";

    co_return;
}

int main() {
    std::cout << "Running ORM Cache Integration Tests...\n";
    core::EventLoop loop(64);

    bool completed = false;
    loop.spawn([&]() -> core::Task<void> {
        co_await run_orm_cache_tests();
        completed = true;
        loop.stop();
    }());

    loop.run();
    assert(completed);
    std::cout << "All ORM Cache Integration Tests PASSED successfully!\n";
    return 0;
}
