#include "data/orm/sql/Sql.h"
#include "data/orm/sql/drivers/SqliteDriver.h"
#include "data/orm/sql/drivers/PostgresDriver.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <cmath>

#define TEST_CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace aegon::core;
using namespace aegon::data::orm::sql;
using namespace aegon::data::orm::sql::drivers;

// ----------------------------------------------------------------------------
// Entities
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

    HasOne<Profile> profile;
    HasMany<OrderRecord> orders;
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
// Test Runner
// ----------------------------------------------------------------------------
Task<void> run_advanced_queries_suite(SqlDatabaseClient& db, DatabaseDialect dialect) {
    // 1. Reset tables
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

    // 2. Seed Data
    // User 1: Alice (has profile, 3 orders [50, 150, 250], 2 roles ["Admin", "Editor"])
    UserAccount u1;
    u1.username = "Alice";
    u1.email = "alice@example.com";
    u1.profile.set_value(Profile{.bio = "System Admin & Architect", .avatar_url = "/avatars/alice.png"});
    u1.orders.push_back(OrderRecord{.item_name = "Keyboard", .price = 50.0});
    u1.orders.push_back(OrderRecord{.item_name = "Monitor", .price = 150.0});
    u1.orders.push_back(OrderRecord{.item_name = "GPU", .price = 250.0});
    u1.roles.push_back(Role{.name = "Admin"});
    u1.roles.push_back(Role{.name = "Editor"});
    co_await db.insert_tree(u1);

    // User 2: Bob (has profile, 1 order [80], 1 role ["Viewer"])
    UserAccount u2;
    u2.username = "Bob";
    u2.email = "bob@example.com";
    u2.profile.set_value(Profile{.bio = "Frontend Contributor", .avatar_url = "/avatars/bob.png"});
    u2.orders.push_back(OrderRecord{.item_name = "Mouse", .price = 80.0});
    u2.roles.push_back(Role{.name = "Viewer"});
    co_await db.insert_tree(u2);

    // User 3: Charlie (NO profile, NO orders, NO roles)
    UserAccount u3;
    u3.username = "Charlie";
    u3.email = "charlie@example.com";
    co_await db.insert_tree(u3);

    // ------------------------------------------------------------------------
    // Part A: Option 1 - where_has & where_doesnt_have
    // ------------------------------------------------------------------------
    std::cout << "  [A] Testing where_has and where_doesnt_have (Option 1)..." << std::endl;

    // 1:1 tests
    {
        auto with_profile = co_await db.fetch_all(
            from<UserAccount>().where_has(&UserAccount::profile).order_by(&UserAccount::id, SortOrder::Asc)
        );
        TEST_CHECK(with_profile.size() == 2);
        TEST_CHECK(with_profile[0].username == "Alice");
        TEST_CHECK(with_profile[1].username == "Bob");

        auto without_profile = co_await db.fetch_all(
            from<UserAccount>().where_doesnt_have(&UserAccount::profile)
        );
        TEST_CHECK(without_profile.size() == 1);
        TEST_CHECK(without_profile[0].username == "Charlie");

        auto admin_profile = co_await db.fetch_all(
            from<UserAccount>().where_has(&UserAccount::profile, [](auto& q) {
                q.where(&Profile::bio, Op::Like, "%Architect%");
            })
        );
        TEST_CHECK(admin_profile.size() == 1);
        TEST_CHECK(admin_profile[0].username == "Alice");
    }

    // 1:N tests
    {
        auto with_orders = co_await db.fetch_all(
            from<UserAccount>().where_has(&UserAccount::orders).order_by(&UserAccount::id, SortOrder::Asc)
        );
        TEST_CHECK(with_orders.size() == 2);
        TEST_CHECK(with_orders[0].username == "Alice");
        TEST_CHECK(with_orders[1].username == "Bob");

        auto without_orders = co_await db.fetch_all(
            from<UserAccount>().where_doesnt_have(&UserAccount::orders)
        );
        TEST_CHECK(without_orders.size() == 1);
        TEST_CHECK(without_orders[0].username == "Charlie");

        // where_has with child condition
        auto expensive_orders = co_await db.fetch_all(
            from<UserAccount>().where_has(&UserAccount::orders, [](auto& q) {
                q.where(&OrderRecord::price, Op::Gt, 200.0);
            })
        );
        TEST_CHECK(expensive_orders.size() == 1);
        TEST_CHECK(expensive_orders[0].username == "Alice");

        auto moderate_orders = co_await db.fetch_all(
            from<UserAccount>().where_has(&UserAccount::orders, [](auto& q) {
                q.where(&OrderRecord::price, Op::Gte, 80.0);
            }).order_by(&UserAccount::id, SortOrder::Asc)
        );
        TEST_CHECK(moderate_orders.size() == 2);
    }

    // N:M tests
    {
        auto with_roles = co_await db.fetch_all(
            from<UserAccount>().where_has(&UserAccount::roles).order_by(&UserAccount::id, SortOrder::Asc)
        );
        TEST_CHECK(with_roles.size() == 2);
        TEST_CHECK(with_roles[0].username == "Alice");
        TEST_CHECK(with_roles[1].username == "Bob");

        auto without_roles = co_await db.fetch_all(
            from<UserAccount>().where_doesnt_have(&UserAccount::roles)
        );
        TEST_CHECK(without_roles.size() == 1);
        TEST_CHECK(without_roles[0].username == "Charlie");

        auto admin_users = co_await db.fetch_all(
            from<UserAccount>().where_has(&UserAccount::roles, [](auto& q) {
                q.where(&Role::name, Op::Eq, "Admin");
            })
        );
        TEST_CHECK(admin_users.size() == 1);
        TEST_CHECK(admin_users[0].username == "Alice");

        auto not_admin = co_await db.fetch_all(
            from<UserAccount>().where_doesnt_have(&UserAccount::roles, [](auto& q) {
                q.where(&Role::name, Op::Eq, "Admin");
            }).order_by(&UserAccount::id, SortOrder::Asc)
        );
        TEST_CHECK(not_admin.size() == 2);
        TEST_CHECK(not_admin[0].username == "Bob");
        TEST_CHECK(not_admin[1].username == "Charlie");
    }
    std::cout << "    -> where_has and where_doesnt_have passed on 1:1, 1:N, and N:M!" << std::endl;

    // ------------------------------------------------------------------------
    // Part B: Option 1 - Scoped Includes
    // ------------------------------------------------------------------------
    std::cout << "  [B] Testing Scoped Includes (.include(rel, filter)) (Option 1)..." << std::endl;
    {
        // 1:N scoped include with filter and order_by
        auto users = co_await db.fetch_all(
            from<UserAccount>()
                .where(&UserAccount::username, Op::Eq, "Alice")
                .include(&UserAccount::orders, [](auto& q) {
                    q.where(&OrderRecord::price, Op::Gte, 100.0)
                     .order_by(&OrderRecord::price, SortOrder::Desc);
                })
        );
        TEST_CHECK(users.size() == 1);
        TEST_CHECK(users[0].orders.size() == 2);
        TEST_CHECK(users[0].orders[0].price == 250.0);
        TEST_CHECK(users[0].orders[1].price == 150.0);

        // 1:N scoped include with limit (top 1 highest price)
        auto top_order_users = co_await db.fetch_all(
            from<UserAccount>()
                .where(&UserAccount::username, Op::Eq, "Alice")
                .include(&UserAccount::orders, [](auto& q) {
                    q.order_by(&OrderRecord::price, SortOrder::Desc)
                     .limit(1);
                })
        );
        TEST_CHECK(top_order_users.size() == 1);
        TEST_CHECK(top_order_users[0].orders.size() == 1);
        TEST_CHECK(top_order_users[0].orders[0].price == 250.0);

        // N:M scoped include with filter
        auto role_users = co_await db.fetch_all(
            from<UserAccount>()
                .where(&UserAccount::username, Op::Eq, "Alice")
                .include(&UserAccount::roles, [](auto& q) {
                    q.where(&Role::name, Op::Eq, "Editor");
                })
        );
        TEST_CHECK(role_users.size() == 1);
        TEST_CHECK(role_users[0].roles.size() == 1);
        TEST_CHECK(role_users[0].roles[0].name == "Editor");
    }
    std::cout << "    -> Scoped includes passed with custom filters, sorting, and limits!" << std::endl;

    // ------------------------------------------------------------------------
    // Part C: Option 4 - Aggregations (count, sum, avg, min, max)
    // ------------------------------------------------------------------------
    std::cout << "  [C] Testing Aggregations (count, sum, avg, min, max) (Option 4)..." << std::endl;
    {
        // 1. count
        uint64_t total_orders = co_await db.count(from<OrderRecord>());
        TEST_CHECK(total_orders == 4);

        uint64_t high_val_orders = co_await db.count(
            from<OrderRecord>().where(&OrderRecord::price, Op::Gte, 100.0)
        );
        TEST_CHECK(high_val_orders == 2);

        // 2. sum (total = 50 + 150 + 250 + 80 = 530)
        auto total_revenue = co_await db.sum(from<OrderRecord>(), &OrderRecord::price);
        TEST_CHECK(total_revenue.has_value());
        TEST_CHECK(std::abs(*total_revenue - 530.0) < 0.001);

        auto filtered_sum = co_await db.sum(
            from<OrderRecord>().where(&OrderRecord::price, Op::Gte, 100.0),
            &OrderRecord::price
        );
        TEST_CHECK(filtered_sum.has_value());
        TEST_CHECK(std::abs(*filtered_sum - 400.0) < 0.001);

        // 3. avg (530 / 4 = 132.5)
        auto avg_price = co_await db.avg(from<OrderRecord>(), &OrderRecord::price);
        TEST_CHECK(avg_price.has_value());
        TEST_CHECK(std::abs(*avg_price - 132.5) < 0.001);

        // 4. min (50.0)
        auto min_price = co_await db.min(from<OrderRecord>(), &OrderRecord::price);
        TEST_CHECK(min_price.has_value());
        TEST_CHECK(std::abs(*min_price - 50.0) < 0.001);

        // 5. max (250.0)
        auto max_price = co_await db.max(from<OrderRecord>(), &OrderRecord::price);
        TEST_CHECK(max_price.has_value());
        TEST_CHECK(std::abs(*max_price - 250.0) < 0.001);
    }
    std::cout << "    -> Aggregates (count, sum, avg, min, max) verified accurately!" << std::endl;

    // ------------------------------------------------------------------------
    // Part D: Option 4 - Group By & Having
    // ------------------------------------------------------------------------
    std::cout << "  [D] Testing Group By & Having (Option 4)..." << std::endl;
    {
        // Query users with orders >= 2 (only Alice has 3 orders, Bob has 1)
        auto builder = from<OrderRecord>()
            .select(&OrderRecord::user_id)
            .group_by(&OrderRecord::user_id)
            .having("COUNT(*) >= 2");
        auto query = builder.to_sql(dialect);
        auto rows = co_await db.execute(query);
        TEST_CHECK(rows >= 1);
    }
    std::cout << "    -> Group By and Having clauses compiled and executed correctly!" << std::endl;

    // ------------------------------------------------------------------------
    // Part E: Option 4 - Pagination (Page<T> and paginate)
    // ------------------------------------------------------------------------
    std::cout << "  [E] Testing Pagination (.paginate(builder, page, per_page)) (Option 4)..." << std::endl;
    {
        // Page 1 of users (per_page = 2, total = 3)
        auto page1 = co_await db.paginate(
            from<UserAccount>().order_by(&UserAccount::id, SortOrder::Asc),
            /*page=*/1,
            /*per_page=*/2
        );
        TEST_CHECK(page1.total_items == 3);
        TEST_CHECK(page1.current_page == 1);
        TEST_CHECK(page1.per_page == 2);
        TEST_CHECK(page1.total_pages == 2);
        TEST_CHECK(page1.has_next == true);
        TEST_CHECK(page1.has_prev == false);
        TEST_CHECK(page1.items.size() == 2);
        TEST_CHECK(page1.items[0].username == "Alice");
        TEST_CHECK(page1.items[1].username == "Bob");

        // Page 2 of users
        auto page2 = co_await db.paginate(
            from<UserAccount>().order_by(&UserAccount::id, SortOrder::Asc),
            /*page=*/2,
            /*per_page=*/2
        );
        TEST_CHECK(page2.total_items == 3);
        TEST_CHECK(page2.current_page == 2);
        TEST_CHECK(page2.per_page == 2);
        TEST_CHECK(page2.total_pages == 2);
        TEST_CHECK(page2.has_next == false);
        TEST_CHECK(page2.has_prev == true);
        TEST_CHECK(page2.items.size() == 1);
        TEST_CHECK(page2.items[0].username == "Charlie");

        // Paginate WITH Eager Includes!
        auto page_with_includes = co_await db.paginate(
            from<UserAccount>()
                .include(&UserAccount::orders)
                .include(&UserAccount::profile)
                .order_by(&UserAccount::id, SortOrder::Asc),
            /*page=*/1,
            /*per_page=*/2
        );
        TEST_CHECK(page_with_includes.items.size() == 2);
        TEST_CHECK(page_with_includes.items[0].orders.size() == 3);
        TEST_CHECK(page_with_includes.items[0].profile.has_value());
        TEST_CHECK(page_with_includes.items[1].orders.size() == 1);
        TEST_CHECK(page_with_includes.items[1].profile.has_value());
    }
    std::cout << "    -> Pagination verified across pages and with eager includes!" << std::endl;
}

void test_advanced_queries_sqlite() {
    std::cout << "[TEST 1] Testing on SQLite (In-Memory)..." << std::endl;
    auto pool = create_sqlite_pool(":memory:", 2);
    SqlDatabaseClient db(*pool);

    auto task = run_advanced_queries_suite(db, DatabaseDialect::SQLite);
    task.resume();
    task.result();
    std::cout << " -> SQLite Advanced Queries Test Passed Successfully!" << std::endl;
}

void test_advanced_queries_postgres() {
    std::cout << "[TEST 2] Testing on Live PostgreSQL 17..." << std::endl;
    std::string conninfo = "host=127.0.0.1 port=5432 dbname=aegon_test user=postgres password=postgres";
    auto pool = create_postgres_pool(conninfo, 4);
    SqlDatabaseClient db(*pool);

    auto task = run_advanced_queries_suite(db, DatabaseDialect::PostgreSQL);
    task.resume();
    task.result();
    std::cout << " -> Live PostgreSQL 17 Advanced Queries Test Passed Successfully!" << std::endl;
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << "   Aegon SQL ORM Advanced Queries & Analytics Test Suite" << std::endl;
    std::cout << "   (where_has, scoped includes, aggregates, paginate)   " << std::endl;
    std::cout << "========================================================" << std::endl;

    test_advanced_queries_sqlite();
    test_advanced_queries_postgres();

    std::cout << "========================================================" << std::endl;
    std::cout << "   ALL ADVANCED QUERIES & ANALYTICS TESTS PASSED!       " << std::endl;
    std::cout << "========================================================" << std::endl;
    return 0;
}
