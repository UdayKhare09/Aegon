#include "data/orm/sql/Sql.h"
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

using namespace aegon::data;
using namespace aegon::data::orm::sql;

// 1. User POCO Entity
struct User {
    UUID id;
    std::string email;
    std::string username;
    std::optional<std::string> bio;
    Decimal128 balance;
    Json preferences;
    IpAddress last_login_ip;
    DateTime created_at;
    DateTime updated_at;

    static auto schema() {
        return table<User>("users")
            .id(&User::id)
            .column(&User::email, "email").unique().length(255)
            .column(&User::username, "username").indexed().length(50)
            .column(&User::bio, "bio")
            .column(&User::balance, "balance").default_value("0.0000")
            .column(&User::preferences, "preferences")
            .column(&User::last_login_ip, "last_login_ip")
            .created_at(&User::created_at)
            .updated_at(&User::updated_at);
    }
};

// 2. Order POCO Entity with Foreign Key to User
struct Order {
    UUID id;
    UUID user_id;
    Decimal128 total;
    std::string status;
    DateTime created_at;

    static auto schema() {
        return table<Order>("orders")
            .id(&Order::id)
            .column(&Order::user_id, "user_id").references<User>(&User::id).on_delete_cascade()
            .column(&Order::total, "total")
            .column(&Order::status, "status").default_value("'PENDING'")
            .created_at(&Order::created_at);
    }
};

// 3. Post POCO Entity with Auto-Increment BIGINT Primary Key
struct Post {
    int64_t id{0};
    UUID author_id;
    std::string title;
    std::string body;

    static auto schema() {
        return table<Post>("posts")
            .id(&Post::id)
            .column(&Post::author_id, "author_id").references<User>(&User::id).on_delete_cascade()
            .column(&Post::title, "title").length(200)
            .column(&Post::body, "body");
    }
};

// 4. UserRole POCO Entity with Composite Primary Key & Multi-Column Indexes
struct UserRole {
    int64_t user_id{0};
    int64_t role_id{0};
    DateTime assigned_at;

    static auto schema() {
        return table<UserRole>("user_roles")
            .column(&UserRole::user_id, "user_id")
            .column(&UserRole::role_id, "role_id")
            .created_at(&UserRole::assigned_at, "assigned_at")
            .composite_primary_key(&UserRole::user_id, &UserRole::role_id)
            .add_index("idx_user_roles_composite", &UserRole::role_id, &UserRole::assigned_at)
            .add_unique_index("uq_user_roles_pair", &UserRole::user_id, &UserRole::role_id);
    }
};

void test_dialect_traits() {
    std::cout << "[Test 1] Testing Dialect Traits (Quoting, Placeholders, Auto-inc)...\n";

    // Quoting
    TEST_CHECK(DialectTraits::quote_char(DatabaseDialect::PostgreSQL) == '"');
    TEST_CHECK(DialectTraits::quote_char(DatabaseDialect::SQLite) == '"');

    TEST_CHECK(DialectTraits::quote_identifier(DatabaseDialect::PostgreSQL, "users") == "\"users\"");
    TEST_CHECK(DialectTraits::quote_identifier(DatabaseDialect::SQLite, "users") == "\"users\"");

    // Placeholders
    std::string p_pg;
    DialectTraits::format_placeholder(DatabaseDialect::PostgreSQL, 1, p_pg);
    TEST_CHECK(p_pg == "$1");

    std::string p_sqlite;
    DialectTraits::format_placeholder(DatabaseDialect::SQLite, 1, p_sqlite);
    TEST_CHECK(p_sqlite == "?");

    // Auto-inc PK
    TEST_CHECK(DialectTraits::auto_increment_pk(DatabaseDialect::PostgreSQL).find("IDENTITY") != std::string::npos);
    TEST_CHECK(DialectTraits::auto_increment_pk(DatabaseDialect::SQLite).find("AUTOINCREMENT") != std::string::npos);

    std::cout << "  -> PASS: Dialect traits verified.\n";
}

void test_type_mapper() {
    std::cout << "[Test 2] Testing TypeMapper for Primitives and Foundational Types...\n";

    // Primitives
    TEST_CHECK(TypeMapper<bool>::column_type(DatabaseDialect::PostgreSQL) == "BOOLEAN");
    TEST_CHECK(TypeMapper<bool>::column_type(DatabaseDialect::SQLite) == "INTEGER");

    TEST_CHECK(TypeMapper<int32_t>::column_type(DatabaseDialect::PostgreSQL) == "INTEGER");

    TEST_CHECK(TypeMapper<std::string>::column_type(DatabaseDialect::PostgreSQL, 255) == "VARCHAR(255)");
    TEST_CHECK(TypeMapper<std::string>::column_type(DatabaseDialect::PostgreSQL, 0) == "TEXT");

    // Foundational Types
    TEST_CHECK(TypeMapper<UUID>::column_type(DatabaseDialect::PostgreSQL) == "UUID");
    TEST_CHECK(TypeMapper<UUID>::column_type(DatabaseDialect::SQLite) == "TEXT");

    TEST_CHECK(TypeMapper<DateTime>::column_type(DatabaseDialect::PostgreSQL) == "TIMESTAMPTZ");
    TEST_CHECK(TypeMapper<DateTime>::column_type(DatabaseDialect::SQLite) == "TEXT");

    TEST_CHECK(TypeMapper<Decimal128>::column_type(DatabaseDialect::PostgreSQL) == "NUMERIC(18, 4)");
    TEST_CHECK(TypeMapper<Decimal128>::column_type(DatabaseDialect::SQLite) == "NUMERIC");

    TEST_CHECK(TypeMapper<Json>::column_type(DatabaseDialect::PostgreSQL) == "JSONB");
    TEST_CHECK(TypeMapper<Json>::column_type(DatabaseDialect::SQLite) == "TEXT");

    TEST_CHECK(TypeMapper<IpAddress>::column_type(DatabaseDialect::PostgreSQL) == "INET");

    TEST_CHECK(TypeMapper<MacAddress>::column_type(DatabaseDialect::PostgreSQL) == "MACADDR");
    TEST_CHECK(TypeMapper<Blob>::column_type(DatabaseDialect::PostgreSQL) == "BYTEA");
    TEST_CHECK(TypeMapper<Blob>::column_type(DatabaseDialect::SQLite) == "BLOB");
    TEST_CHECK(TypeMapper<Hash256>::column_type(DatabaseDialect::PostgreSQL) == "BYTEA");

    // Nullability
    TEST_CHECK(!TypeMapper<std::string>::is_nullable);
    TEST_CHECK(TypeMapper<std::optional<std::string>>::is_nullable);
    TEST_CHECK(TypeMapper<std::optional<UUID>>::is_nullable);

    std::cout << "  -> PASS: All TypeMapper dialect specializations verified.\n";
}

void test_postgresql_ddl() {
    std::cout << "[Test 3] Testing PostgreSQL DDL Generation for User Entity...\n";

    std::string ddl = generate_ddl<User>(DatabaseDialect::PostgreSQL);
    std::cout << "--- Generated PostgreSQL DDL ---\n" << ddl << "\n--------------------------------\n";

    TEST_CHECK(ddl.find("CREATE TABLE IF NOT EXISTS \"users\" (") != std::string::npos);
    TEST_CHECK(ddl.find("\"id\" UUID PRIMARY KEY") != std::string::npos);
    TEST_CHECK(ddl.find("\"email\" VARCHAR(255) NOT NULL UNIQUE") != std::string::npos);
    TEST_CHECK(ddl.find("\"username\" VARCHAR(50) NOT NULL") != std::string::npos);
    TEST_CHECK(ddl.find("\"bio\" TEXT,\n") != std::string::npos); // Nullable, no NOT NULL!
    TEST_CHECK(ddl.find("\"balance\" NUMERIC(18, 4) NOT NULL DEFAULT 0.0000") != std::string::npos);
    TEST_CHECK(ddl.find("\"preferences\" JSONB NOT NULL") != std::string::npos);
    TEST_CHECK(ddl.find("\"last_login_ip\" INET NOT NULL") != std::string::npos);
    TEST_CHECK(ddl.find("\"created_at\" TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP") != std::string::npos);
    TEST_CHECK(ddl.find("\"updated_at\" TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP") != std::string::npos);

    // Test Indexes
    auto indexes = generate_indexes<User>(DatabaseDialect::PostgreSQL);
    TEST_CHECK(indexes.size() == 1);
    TEST_CHECK(indexes[0] == "CREATE INDEX IF NOT EXISTS idx_users_username ON \"users\" (\"username\");");

    // Test Drop Table
    std::string drop = generate_drop_table<User>(DatabaseDialect::PostgreSQL, true, true);
    TEST_CHECK(drop == "DROP TABLE IF EXISTS \"users\" CASCADE;");

    std::cout << "  -> PASS: PostgreSQL DDL perfectly matches native schema.\n";
}

void test_sqlite_ddl() {
    std::cout << "[Test 4] Testing SQLite DDL Generation for User Entity...\n";

    std::string ddl = generate_ddl<User>(DatabaseDialect::SQLite);
    std::cout << "--- Generated SQLite DDL ---\n" << ddl << "\n----------------------------\n";

    TEST_CHECK(ddl.find("CREATE TABLE IF NOT EXISTS \"users\" (") != std::string::npos);
    TEST_CHECK(ddl.find("\"id\" TEXT PRIMARY KEY") != std::string::npos);
    TEST_CHECK(ddl.find("\"email\" VARCHAR(255) NOT NULL UNIQUE") != std::string::npos);
    TEST_CHECK(ddl.find("\"balance\" NUMERIC NOT NULL DEFAULT 0.0000") != std::string::npos);
    TEST_CHECK(ddl.find("\"preferences\" TEXT NOT NULL") != std::string::npos);
    TEST_CHECK(ddl.find("\"created_at\" TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP") != std::string::npos);

    std::cout << "  -> PASS: SQLite DDL verified.\n";
}

void test_foreign_keys_and_auto_inc() {
    std::cout << "[Test 5] Testing Foreign Key Relationships & Auto-Increment Primary Keys...\n";

    // 1. Order entity referencing User with ON DELETE CASCADE
    std::string order_ddl = generate_ddl<Order>(DatabaseDialect::PostgreSQL);
    std::cout << "--- Generated Order DDL ---\n" << order_ddl << "\n---------------------------\n";

    TEST_CHECK(order_ddl.find("CONSTRAINT fk_orders_user_id FOREIGN KEY (\"user_id\") REFERENCES \"users\" (\"id\") ON DELETE CASCADE") != std::string::npos);
    TEST_CHECK(order_ddl.find("\"status\" TEXT NOT NULL DEFAULT 'PENDING'") != std::string::npos);

    // 2. Post entity with Auto-Increment BIGINT PK
    std::string post_pg = generate_ddl<Post>(DatabaseDialect::PostgreSQL);
    TEST_CHECK(post_pg.find("\"id\" BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY") != std::string::npos);

    std::string post_sqlite = generate_ddl<Post>(DatabaseDialect::SQLite);
    TEST_CHECK(post_sqlite.find("\"id\" INTEGER PRIMARY KEY AUTOINCREMENT") != std::string::npos);

    std::cout << "  -> PASS: Strongly-typed foreign keys and auto-increment PKs verified.\n";
}

void test_composite_pk_and_indexes() {
    std::cout << "[Test 6] Testing Composite Primary Keys & Table-Level Constraints...\n";

    // 1. PostgreSQL DDL for UserRole with Composite PK
    std::string ddl_pg = generate_ddl<UserRole>(DatabaseDialect::PostgreSQL);
    std::cout << "--- Generated UserRole PG DDL ---\n" << ddl_pg << "\n---------------------------------\n";

    TEST_CHECK(ddl_pg.find("CREATE TABLE IF NOT EXISTS \"user_roles\" (") != std::string::npos);
    TEST_CHECK(ddl_pg.find("\"user_id\" BIGINT NOT NULL") != std::string::npos);
    TEST_CHECK(ddl_pg.find("\"role_id\" BIGINT NOT NULL") != std::string::npos);
    TEST_CHECK(ddl_pg.find("PRIMARY KEY (\"user_id\", \"role_id\")") != std::string::npos);
    TEST_CHECK(ddl_pg.find("CONSTRAINT uq_user_roles_pair UNIQUE (\"user_id\", \"role_id\")") != std::string::npos);

    // 2. SQLite DDL for UserRole with Composite PK
    std::string ddl_sqlite = generate_ddl<UserRole>(DatabaseDialect::SQLite);
    std::cout << "--- Generated UserRole SQLite DDL ---\n" << ddl_sqlite << "\n-------------------------------------\n";

    TEST_CHECK(ddl_sqlite.find("CREATE TABLE IF NOT EXISTS \"user_roles\" (") != std::string::npos);
    TEST_CHECK(ddl_sqlite.find("PRIMARY KEY (\"user_id\", \"role_id\")") != std::string::npos);

    // 3. Multi-Column Composite Index Generation
    auto indexes = generate_indexes<UserRole>(DatabaseDialect::PostgreSQL);
    TEST_CHECK(!indexes.empty());
    bool found_composite_idx = false;
    for (const auto& idx : indexes) {
        if (idx.find("idx_user_roles_composite") != std::string::npos &&
            idx.find("(\"role_id\", \"assigned_at\")") != std::string::npos) {
            found_composite_idx = true;
        }
    }
    TEST_CHECK(found_composite_idx);

    std::cout << "  -> PASS: Composite primary keys and multi-column indexes verified.\n";
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "    AEGON C++26 SQL ORM SCHEMA & DIALECT TEST SUITE    \n";
    std::cout << "=======================================================\n\n";

    test_dialect_traits();
    test_type_mapper();
    test_postgresql_ddl();
    test_sqlite_ddl();
    test_foreign_keys_and_auto_inc();
    test_composite_pk_and_indexes();

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL SQL ORM SCHEMA TESTS PASSED! <<<\n";
    std::cout << "=======================================================\n\n";
    return 0;
}
