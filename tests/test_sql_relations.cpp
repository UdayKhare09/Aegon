#include "data/orm/sql/Sql.h"
#include "data/orm/sql/drivers/SqliteDriver.h"
#include "data/orm/sql/drivers/PostgresDriver.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>

#define TEST_CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace aegon::core;
using namespace aegon::data::orm::sql;
using namespace aegon::data::orm::sql::drivers;

// Forward declarations
struct Profile;
struct OrderRecord;
struct Role;
struct UserRole;
struct UserAccount;

// ----------------------------------------------------------------------------
// Entity Definitions
// ----------------------------------------------------------------------------

struct Profile {
    int64_t id{0};
    int64_t user_id{0};
    std::string bio;
    std::string avatar_url;

    static auto schema() {
        return table<Profile>("profiles")
            .id(&Profile::id)
            .column(&Profile::user_id, "user_id")
            .column(&Profile::bio, "bio")
            .column(&Profile::avatar_url, "avatar_url");
    }
};

struct OrderRecord {
    int64_t id{0};
    int64_t user_id{0};
    std::string item_name;
    double price{0.0};

    static auto schema() {
        return table<OrderRecord>("orders")
            .id(&OrderRecord::id)
            .column(&OrderRecord::user_id, "user_id")
            .column(&OrderRecord::item_name, "item_name")
            .column(&OrderRecord::price, "price");
    }
};

struct Role {
    int64_t id{0};
    std::string name;

    static auto schema() {
        return table<Role>("roles")
            .id(&Role::id)
            .column(&Role::name, "name");
    }
};

struct UserRole {
    int64_t user_id{0};
    int64_t role_id{0};

    static auto schema() {
        return table<UserRole>("user_roles")
            .column(&UserRole::user_id, "user_id")
            .column(&UserRole::role_id, "role_id");
    }
};

struct UserAccount {
    int64_t id{0};
    std::string username;
    std::string email;

    // 1:1 Relation
    HasOne<Profile> profile;

    // 1:N Relation
    HasMany<OrderRecord> orders;

    // N:M Relation (through UserRole junction table)
    HasMany<Role> roles;

    static auto schema() {
        return table<UserAccount>("users")
            .id(&UserAccount::id)
            .column(&UserAccount::username, "username")
            .column(&UserAccount::email, "email")
            .has_one(&UserAccount::profile, &Profile::user_id)
            .has_many(&UserAccount::orders, &OrderRecord::user_id)
            .has_many(&UserAccount::roles).through<UserRole>(&UserRole::user_id, &UserRole::role_id);
    }
};

// ----------------------------------------------------------------------------
// Test Runner for any SQL Database Client
// ----------------------------------------------------------------------------
Task<void> run_relations_test_suite(SqlDatabaseClient& db, DatabaseDialect dialect) {
    // 1. Setup Tables
    if (dialect == DatabaseDialect::PostgreSQL) {
        co_await db.execute("DROP TABLE IF EXISTS user_roles CASCADE;");
        co_await db.execute("DROP TABLE IF EXISTS orders CASCADE;");
        co_await db.execute("DROP TABLE IF EXISTS profiles CASCADE;");
        co_await db.execute("DROP TABLE IF EXISTS roles CASCADE;");
        co_await db.execute("DROP TABLE IF EXISTS users CASCADE;");
    } else {
        co_await db.execute("DROP TABLE IF EXISTS user_roles;");
        co_await db.execute("DROP TABLE IF EXISTS orders;");
        co_await db.execute("DROP TABLE IF EXISTS profiles;");
        co_await db.execute("DROP TABLE IF EXISTS roles;");
        co_await db.execute("DROP TABLE IF EXISTS users;");
    }

    co_await db.execute(generate_ddl<UserAccount>(dialect));
    co_await db.execute(generate_ddl<Profile>(dialect));
    co_await db.execute(generate_ddl<OrderRecord>(dialect));
    co_await db.execute(generate_ddl<Role>(dialect));
    co_await db.execute(generate_ddl<UserRole>(dialect));

    // 2. Insert Users
    int64_t u1_id = co_await db.insert_get_id(UserAccount{.id = 0, .username = "alice", .email = "alice@aegon.io"});
    int64_t u2_id = co_await db.insert_get_id(UserAccount{.id = 0, .username = "bob", .email = "bob@aegon.io"});
    int64_t u3_id = co_await db.insert_get_id(UserAccount{.id = 0, .username = "charlie", .email = "charlie@aegon.io"});

    TEST_CHECK(u1_id > 0);
    TEST_CHECK(u2_id > 0);
    TEST_CHECK(u3_id > 0);

    // 3. Insert 1:1 Profiles
    co_await db.insert(Profile{.id = 0, .user_id = u1_id, .bio = "Systems Engineer", .avatar_url = "https://aegon.io/alice.png"});
    co_await db.insert(Profile{.id = 0, .user_id = u2_id, .bio = "Database Architect", .avatar_url = "https://aegon.io/bob.png"});
    // Note: Charlie has NO profile (testing std::nullopt handling)

    // 4. Insert 1:N Orders
    // Alice has 2 orders
    co_await db.insert(OrderRecord{.id = 0, .user_id = u1_id, .item_name = "Mechanical Keyboard", .price = 150.0});
    co_await db.insert(OrderRecord{.id = 0, .user_id = u1_id, .item_name = "4K Monitor", .price = 450.0});
    // Bob has 1 order
    co_await db.insert(OrderRecord{.id = 0, .user_id = u2_id, .item_name = "NVMe SSD 2TB", .price = 180.0});
    // Charlie has 0 orders

    // 5. Insert Roles & N:M UserRoles
    int64_t r_admin = co_await db.insert_get_id(Role{.id = 0, .name = "Admin"});
    int64_t r_dev = co_await db.insert_get_id(Role{.id = 0, .name = "Developer"});
    int64_t r_support = co_await db.insert_get_id(Role{.id = 0, .name = "Support"});

    // Alice -> Admin, Developer
    co_await db.link<UserRole>(u1_id, r_admin);
    co_await db.link<UserRole>(u1_id, r_dev);

    // Bob -> Developer, Support
    co_await db.link<UserRole>(u2_id, r_dev);
    co_await db.link<UserRole>(u2_id, r_support);

    // Charlie -> Support
    co_await db.link<UserRole>(u3_id, r_support);

    // =========================================================================
    // TEST SECTION A: Eager Loading via .include()
    // =========================================================================
    std::cout << "  [A] Testing Eager Loading (.include() for 1:1, 1:N, N:M)...\n";
    {
        auto users = co_await db.fetch_all(
            db.from<UserAccount>()
              .include(&UserAccount::profile)
              .include(&UserAccount::orders)
              .include(&UserAccount::roles)
              .order_by(&UserAccount::id, SortOrder::Asc)
        );

        TEST_CHECK(users.size() == 3);

        // Verify User 1: Alice
        auto& alice = users[0];
        TEST_CHECK(alice.id == u1_id);
        TEST_CHECK(alice.username == "alice");

        // 1:1 Eager Profile
        TEST_CHECK(alice.profile.is_loaded());
        TEST_CHECK(alice.profile.has_value());
        TEST_CHECK(alice.profile->bio == "Systems Engineer");
        TEST_CHECK(alice.profile->avatar_url == "https://aegon.io/alice.png");

        // 1:N Eager Orders
        TEST_CHECK(alice.orders.is_loaded());
        TEST_CHECK(alice.orders.size() == 2);
        TEST_CHECK(alice.orders[0].item_name == "Mechanical Keyboard");
        TEST_CHECK(alice.orders[1].item_name == "4K Monitor");

        // N:M Eager Roles
        TEST_CHECK(alice.roles.is_loaded());
        TEST_CHECK(alice.roles.size() == 2);
        bool has_admin = false, has_dev = false;
        for (const auto& r : alice.roles) {
            if (r.name == "Admin") has_admin = true;
            if (r.name == "Developer") has_dev = true;
        }
        TEST_CHECK(has_admin && has_dev);

        // Verify User 2: Bob
        auto& bob = users[1];
        TEST_CHECK(bob.id == u2_id);
        TEST_CHECK(bob.profile.is_loaded());
        TEST_CHECK(bob.profile.has_value());
        TEST_CHECK(bob.profile->bio == "Database Architect");
        TEST_CHECK(bob.orders.is_loaded());
        TEST_CHECK(bob.orders.size() == 1);
        TEST_CHECK(bob.orders[0].item_name == "NVMe SSD 2TB");
        TEST_CHECK(bob.roles.is_loaded());
        TEST_CHECK(bob.roles.size() == 2);

        // Verify User 3: Charlie (Null profile, 0 orders, 1 role)
        auto& charlie = users[2];
        TEST_CHECK(charlie.id == u3_id);
        TEST_CHECK(charlie.profile.is_loaded());
        TEST_CHECK(!charlie.profile.has_value()); // Null profile correctly preserved!
        TEST_CHECK(charlie.orders.is_loaded());
        TEST_CHECK(charlie.orders.empty());
        TEST_CHECK(charlie.roles.is_loaded());
        TEST_CHECK(charlie.roles.size() == 1);
        TEST_CHECK(charlie.roles[0].name == "Support");

        std::cout << "    -> Eager loading verified with 100% correct stitching across all 3 relation types!\n";
    }

    // =========================================================================
    // TEST SECTION B: Lazy Loading via co_await .load()
    // =========================================================================
    std::cout << "  [B] Testing Lazy Loading (co_await .load() on demand)...\n";
    {
        // Fetch Alice WITHOUT any .include()
        auto opt_alice = co_await db.find_by_id<UserAccount>(u1_id);
        TEST_CHECK(opt_alice.has_value());
        auto& alice = *opt_alice;

        // Initially unpopulated
        TEST_CHECK(!alice.profile.is_loaded());
        TEST_CHECK(!alice.orders.is_loaded());
        TEST_CHECK(!alice.roles.is_loaded());

        // 1. Lazy load 1:1 Profile
        co_await alice.profile.load(db);
        TEST_CHECK(alice.profile.is_loaded());
        TEST_CHECK(alice.profile.has_value());
        TEST_CHECK(alice.profile->bio == "Systems Engineer");

        // 2. Lazy load 1:N Orders
        co_await alice.orders.load(db);
        TEST_CHECK(alice.orders.is_loaded());
        TEST_CHECK(alice.orders.size() == 2);
        TEST_CHECK(alice.orders[0].item_name == "Mechanical Keyboard");

        // 3. Lazy load N:M Roles using client.load syntax
        co_await db.load(alice.roles);
        TEST_CHECK(alice.roles.is_loaded());
        TEST_CHECK(alice.roles.size() == 2);

        // 4. Repeated load should be a no-op (cached)
        co_await alice.roles.load(db);
        TEST_CHECK(alice.roles.size() == 2);

        // 5. Test Lazy Load on Charlie's null profile
        auto opt_charlie = co_await db.find_by_id<UserAccount>(u3_id);
        TEST_CHECK(opt_charlie.has_value());
        TEST_CHECK(!opt_charlie->profile.is_loaded());
        co_await opt_charlie->profile.load(db);
        TEST_CHECK(opt_charlie->profile.is_loaded());
        TEST_CHECK(!opt_charlie->profile.has_value());

        std::cout << "    -> Lazy loading on demand verified with cached semantics!\n";
    }

    // =========================================================================
    // TEST SECTION C: Many-to-Many Link & Unlink Operations
    // =========================================================================
    std::cout << "  [C] Testing Many-to-Many Linking and Unlinking...\n";
    {
        // Unlink Developer role from Alice
        bool unlinked = co_await db.unlink<UserRole>(u1_id, r_dev);
        TEST_CHECK(unlinked);

        // Fresh load Alice and lazy load roles
        auto alice_refreshed = co_await db.find_by_id<UserAccount>(u1_id);
        TEST_CHECK(alice_refreshed.has_value());
        co_await alice_refreshed->roles.load(db);
        TEST_CHECK(alice_refreshed->roles.size() == 1);
        TEST_CHECK(alice_refreshed->roles[0].name == "Admin");

        // Re-link Developer role to Alice
        co_await db.link<UserRole>(u1_id, r_dev);

        auto alice_relinked = co_await db.find_by_id<UserAccount>(u1_id);
        co_await alice_relinked->roles.load(db);
        TEST_CHECK(alice_relinked->roles.size() == 2);

        std::cout << "    -> Link and unlink operations verified!\n";
    }

    co_return;
}

void test_relations_sqlite() {
    std::cout << "[TEST 1] Testing Relations (1:1, 1:N, N:M) on SQLite...\n";
    auto pool = create_sqlite_pool(":memory:", 2);
    SqlDatabaseClient db(*pool);

    auto task = run_relations_test_suite(db, DatabaseDialect::SQLite);
    task.resume();
    std::cout << " -> SQLite Relations Test Passed Successfully!\n";
}

void test_relations_postgres() {
    std::cout << "[TEST 2] Testing Relations (1:1, 1:N, N:M) on Live PostgreSQL 17...\n";
    std::string conninfo = "host=127.0.0.1 port=5432 dbname=aegon_test user=postgres password=postgres";
    auto pool = create_postgres_pool(conninfo, 4);
    SqlDatabaseClient db(*pool);

    auto task = run_relations_test_suite(db, DatabaseDialect::PostgreSQL);
    task.resume();
    std::cout << " -> Live PostgreSQL 17 Relations Test Passed Successfully!\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "   Aegon SQL ORM First-Class Relations Test Suite       \n";
    std::cout << "   (1:1 HasOne, 1:N HasMany, N:M ManyToMany Through)    \n";
    std::cout << "========================================================\n";

    test_relations_sqlite();
    test_relations_postgres();

    std::cout << "========================================================\n";
    std::cout << "   ALL FIRST-CLASS RELATIONS TESTS PASSED!              \n";
    std::cout << "========================================================\n";
    return 0;
}
