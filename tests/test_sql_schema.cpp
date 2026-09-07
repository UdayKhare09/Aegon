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

void test_dialect_traits() {
    std::cout << "[Test 1] Testing Dialect Traits (Quoting, Placeholders, Auto-inc)...\n";

    // Quoting
    TEST_CHECK(DialectTraits::quote_char(DatabaseDialect::PostgreSQL) == '"');
    TEST_CHECK(DialectTraits::quote_char(DatabaseDialect::MySQL) == '`');
    TEST_CHECK(DialectTraits::quote_char(DatabaseDialect::SQLite) == '"');

    TEST_CHECK(DialectTraits::quote_identifier(DatabaseDialect::PostgreSQL, "users") == "\"users\"");
    TEST_CHECK(DialectTraits::quote_identifier(DatabaseDialect::MySQL, "users") == "`users`");
    TEST_CHECK(DialectTraits::quote_identifier(DatabaseDialect::SQLite, "users") == "\"users\"");

    // Placeholders
    std::string p_pg;
    DialectTraits::format_placeholder(DatabaseDialect::PostgreSQL, 1, p_pg);
    TEST_CHECK(p_pg == "$1");

    std::string p_my;
    DialectTraits::format_placeholder(DatabaseDialect::MySQL, 1, p_my);
    TEST_CHECK(p_my == "?");

    // Auto-inc PK
    TEST_CHECK(DialectTraits::auto_increment_pk(DatabaseDialect::PostgreSQL).find("IDENTITY") != std::string::npos);
    TEST_CHECK(DialectTraits::auto_increment_pk(DatabaseDialect::MySQL).find("AUTO_INCREMENT") != std::string::npos);
    TEST_CHECK(DialectTraits::auto_increment_pk(DatabaseDialect::SQLite).find("AUTOINCREMENT") != std::string::npos);

    std::cout << "  -> PASS: Dialect traits verified.\n";
}

void test_type_mapper() {
    std::cout << "[Test 2] Testing TypeMapper for Primitives and Foundational Types...\n";

    // Primitives
    TEST_CHECK(TypeMapper<bool>::column_type(DatabaseDialect::PostgreSQL) == "BOOLEAN");
    TEST_CHECK(TypeMapper<bool>::column_type(DatabaseDialect::MySQL) == "TINYINT(1)");
    TEST_CHECK(TypeMapper<bool>::column_type(DatabaseDialect::SQLite) == "INTEGER");

    TEST_CHECK(TypeMapper<int32_t>::column_type(DatabaseDialect::PostgreSQL) == "INTEGER");
    TEST_CHECK(TypeMapper<int32_t>::column_type(DatabaseDialect::MySQL) == "INT");

    TEST_CHECK(TypeMapper<std::string>::column_type(DatabaseDialect::PostgreSQL, 255) == "VARCHAR(255)");
    TEST_CHECK(TypeMapper<std::string>::column_type(DatabaseDialect::PostgreSQL, 0) == "TEXT");

    // Foundational Types
    TEST_CHECK(TypeMapper<UUID>::column_type(DatabaseDialect::PostgreSQL) == "UUID");
    TEST_CHECK(TypeMapper<UUID>::column_type(DatabaseDialect::MySQL) == "BINARY(16)");
    TEST_CHECK(TypeMapper<UUID>::column_type(DatabaseDialect::SQLite) == "TEXT");

    TEST_CHECK(TypeMapper<DateTime>::column_type(DatabaseDialect::PostgreSQL) == "TIMESTAMPTZ");
    TEST_CHECK(TypeMapper<DateTime>::column_type(DatabaseDialect::MySQL) == "DATETIME(6)");
    TEST_CHECK(TypeMapper<DateTime>::column_type(DatabaseDialect::SQLite) == "TEXT");

    TEST_CHECK(TypeMapper<Decimal128>::column_type(DatabaseDialect::PostgreSQL) == "NUMERIC(18, 4)");
    TEST_CHECK(TypeMapper<Decimal128>::column_type(DatabaseDialect::MySQL) == "DECIMAL(18, 4)");
    TEST_CHECK(TypeMapper<Decimal128>::column_type(DatabaseDialect::SQLite) == "NUMERIC");

    TEST_CHECK(TypeMapper<Json>::column_type(DatabaseDialect::PostgreSQL) == "JSONB");
    TEST_CHECK(TypeMapper<Json>::column_type(DatabaseDialect::MySQL) == "JSON");
    TEST_CHECK(TypeMapper<Json>::column_type(DatabaseDialect::SQLite) == "TEXT");

    TEST_CHECK(TypeMapper<IpAddress>::column_type(DatabaseDialect::PostgreSQL) == "INET");
    TEST_CHECK(TypeMapper<IpAddress>::column_type(DatabaseDialect::MySQL) == "VARBINARY(16)");

    TEST_CHECK(TypeMapper<MacAddress>::column_type(DatabaseDialect::PostgreSQL) == "MACADDR");
    TEST_CHECK(TypeMapper<Blob>::column_type(DatabaseDialect::PostgreSQL) == "BYTEA");
    TEST_CHECK(TypeMapper<Blob>::column_type(DatabaseDialect::MySQL) == "LONGBLOB");
    TEST_CHECK(TypeMapper<Hash256>::column_type(DatabaseDialect::MySQL) == "BINARY(32)");

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

void test_mysql_ddl() {
    std::cout << "[Test 4] Testing MySQL DDL Generation for User Entity...\n";

    std::string ddl = generate_ddl<User>(DatabaseDialect::MySQL);
    std::cout << "--- Generated MySQL DDL ---\n" << ddl << "\n---------------------------\n";

    TEST_CHECK(ddl.find("CREATE TABLE IF NOT EXISTS `users` (") != std::string::npos);
    TEST_CHECK(ddl.find("`id` BINARY(16) PRIMARY KEY") != std::string::npos);
    TEST_CHECK(ddl.find("`email` VARCHAR(255) NOT NULL UNIQUE") != std::string::npos);
    TEST_CHECK(ddl.find("`username` VARCHAR(50) NOT NULL") != std::string::npos);
    TEST_CHECK(ddl.find("`bio` TEXT,\n") != std::string::npos);
    TEST_CHECK(ddl.find("`balance` DECIMAL(18, 4) NOT NULL DEFAULT 0.0000") != std::string::npos);
    TEST_CHECK(ddl.find("`preferences` JSON NOT NULL") != std::string::npos);
    TEST_CHECK(ddl.find("`last_login_ip` VARBINARY(16) NOT NULL") != std::string::npos);
    TEST_CHECK(ddl.find("`created_at` DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)") != std::string::npos);
    TEST_CHECK(ddl.find("`updated_at` DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6)") != std::string::npos);

    std::cout << "  -> PASS: MySQL DDL with backticks and ON UPDATE CURRENT_TIMESTAMP verified.\n";
}

void test_sqlite_ddl() {
    std::cout << "[Test 5] Testing SQLite DDL Generation for User Entity...\n";

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
    std::cout << "[Test 6] Testing Foreign Key Relationships & Auto-Increment Primary Keys...\n";

    // 1. Order entity referencing User with ON DELETE CASCADE
    std::string order_ddl = generate_ddl<Order>(DatabaseDialect::PostgreSQL);
    std::cout << "--- Generated Order DDL ---\n" << order_ddl << "\n---------------------------\n";

    TEST_CHECK(order_ddl.find("CONSTRAINT fk_orders_user_id FOREIGN KEY (\"user_id\") REFERENCES \"users\" (\"id\") ON DELETE CASCADE") != std::string::npos);
    TEST_CHECK(order_ddl.find("\"status\" TEXT NOT NULL DEFAULT 'PENDING'") != std::string::npos);

    // 2. Post entity with Auto-Increment BIGINT PK
    std::string post_pg = generate_ddl<Post>(DatabaseDialect::PostgreSQL);
    TEST_CHECK(post_pg.find("\"id\" BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY") != std::string::npos);

    std::string post_my = generate_ddl<Post>(DatabaseDialect::MySQL);
    TEST_CHECK(post_my.find("`id` BIGINT AUTO_INCREMENT PRIMARY KEY") != std::string::npos);

    std::string post_sqlite = generate_ddl<Post>(DatabaseDialect::SQLite);
    TEST_CHECK(post_sqlite.find("\"id\" INTEGER PRIMARY KEY AUTOINCREMENT") != std::string::npos);

    std::cout << "  -> PASS: Strongly-typed foreign keys and auto-increment PKs verified.\n";
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "    AEGON C++26 SQL ORM SCHEMA & DIALECT TEST SUITE    \n";
    std::cout << "=======================================================\n\n";

    test_dialect_traits();
    test_type_mapper();
    test_postgresql_ddl();
    test_mysql_ddl();
    test_sqlite_ddl();
    test_foreign_keys_and_auto_inc();

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL SQL ORM SCHEMA TESTS PASSED! <<<\n";
    std::cout << "=======================================================\n\n";
    return 0;
}
