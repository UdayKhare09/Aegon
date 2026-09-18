#include "data/cache/CacheBackend.h"
#include "data/orm/sql/SqlConfig.h"
#include "data/orm/sql/SqlDatabaseClient.h"
#include "data/orm/sql/drivers/SqliteDriver.h"
#include "core/EventLoop.h"
#include <cassert>
#include <iostream>
#include <unordered_map>
#include <string>

using namespace aegon;
using namespace aegon::data::cache;
using namespace aegon::data::orm::sql;
using namespace aegon::data::orm::sql::drivers;

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
                .invalidation = InvalidationMode::Partitioned,
                .mutation_sync = MutationSync::UpdateOnWrite
            })
            .by_unique(&Account::email)
            .partition_by(&Account::org_id);
        return s;
    }
};

struct UserRole {
    int user_id{0};
    int role_id{0};

    static const auto& schema() {
        static const auto s = TableDef<UserRole>("user_roles")
            .column(&UserRole::user_id, "user_id")
            .column(&UserRole::role_id, "role_id");
        return s;
    }
};

struct Note {
    int id{0};
    int account_id{0};
    std::string text;

    static const auto& schema() {
        static const auto s = TableDef<Note>("notes")
            .id(&Note::id, "id")
            .column(&Note::account_id, "account_id")
            .column(&Note::text, "text");
        return s;
    }
};

core::Task<void> run_tests() {
    auto pool = create_sqlite_pool(":memory:", 1);
    SqlDatabaseClient db(*pool);
    auto cache = std::make_shared<MockCacheBackend>();
    db.set_cache(cache);

    // Setup schema
    co_await db.execute(
        "CREATE TABLE accounts ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "email TEXT UNIQUE NOT NULL, "
        "name TEXT NOT NULL, "
        "org_id INTEGER NOT NULL"
        ");"
    );
    co_await db.execute(
        "CREATE TABLE user_roles ("
        "user_id INTEGER NOT NULL, "
        "role_id INTEGER NOT NULL, "
        "PRIMARY KEY (user_id, role_id)"
        ");"
    );
    co_await db.execute(
        "CREATE TABLE notes ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "account_id INTEGER NOT NULL, "
        "text TEXT NOT NULL"
        ");"
    );

    // --- TEST 1: Feature Parity (tx.insert with auto-increment, tx.find_by_unique, tx.link, tx.unlink) ---
    co_await db.transaction([&](Transaction& tx) -> core::Task<void> {
        Account a{.id = 0, .email = "tx_alice@test.dev", .name = "Tx Alice", .org_id = 42};
        co_await tx.insert(a);
        assert(a.id > 0); // Auto-increment ID assigned into entity!

        auto found = co_await tx.find_by_unique(&Account::email, "tx_alice@test.dev");
        assert(found.has_value());
        assert(found->id == a.id);
        assert(found->name == "Tx Alice");

        // Test tx.link and tx.unlink
        co_await tx.link<UserRole>(a.id, 100);
        auto count_linked = co_await tx.count(tx.from<UserRole>().where(&UserRole::user_id, Op::Eq, a.id));
        assert(count_linked == 1);

        bool unlinked = co_await tx.unlink<UserRole>(a.id, 100);
        assert(unlinked);
        auto count_unlinked = co_await tx.count(tx.from<UserRole>().where(&UserRole::user_id, Op::Eq, a.id));
        assert(count_unlinked == 0);
    });
    std::cout << "[PASS] Transaction feature parity (insert auto-id, find_by_unique, link, unlink)\n";

    // --- TEST 2: Deferred Post-Commit Cache Invalidation on COMMIT ---
    // After Test 1 committed, check that cache received the deferred invalidation/write!
    assert(cache->store.find("accounts:id:1") != cache->store.end());
    assert(cache->store.find("accounts:part:42:epoch") != cache->store.end());
    std::cout << "[PASS] Cache updated post-commit for inserted entity\n";

    // Modify the entity inside a transaction
    co_await db.transaction([&](Transaction& tx) -> core::Task<void> {
        auto opt = co_await tx.find_by_id<Account>(1);
        assert(opt.has_value());
        opt->name = "Alice Updated";
        co_await tx.update_entity(*opt);
        // Note: inside tx, deferred cache is buffered but not yet committed to cache->store
    });

    // Now that transaction committed, cache should have been updated!
    auto cached_after_commit = co_await db.find_by_id<Account>(1);
    assert(cached_after_commit.has_value());
    assert(cached_after_commit->name == "Alice Updated");
    std::cout << "[PASS] Deferred cache invalidation flushed on commit (fresh data served)\n";

    // --- TEST 3: Deferred Cache Safety on ROLLBACK ---
    bool exception_caught = false;
    try {
        co_await db.transaction([&](Transaction& tx) -> core::Task<void> {
            auto opt = co_await tx.find_by_id<Account>(1);
            assert(opt.has_value());
            opt->name = "Alice Poisoned Dirty Write";
            co_await tx.update_entity(*opt);

            // Simulating failure inside transaction!
            throw std::runtime_error("Simulated business error triggering rollback");
        });
    } catch (const std::exception& e) {
        exception_caught = true;
    }
    assert(exception_caught);

    // Verify cache was NOT poisoned with the uncommitted rollback data!
    auto verified_account = co_await db.find_by_id<Account>(1);
    assert(verified_account.has_value());
    assert(verified_account->name == "Alice Updated"); // Still clean, NOT "Alice Poisoned Dirty Write"!
    std::cout << "[PASS] Rollback discarded deferred cache ops: zero dirty reads or cache pollution\n";
}

int main() {
    core::EventLoop loop(64);
    bool completed = false;
    loop.spawn([&]() -> core::Task<void> {
        co_await run_tests();
        completed = true;
        loop.stop();
    }());
    loop.run();
    assert(completed);
    std::cout << "All Transaction feature parity & cache sync tests PASSED!\n";
    return 0;
}
