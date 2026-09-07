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

// 2. Post POCO Entity with Auto-Increment Primary Key
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

void test_member_pointer_resolution() {
    std::cout << "[Test 1] Testing Member-Pointer Column Name Resolution...\n";

    auto schema = User::schema();
    TEST_CHECK(schema.resolve_column_name(&User::id) == "id");
    TEST_CHECK(schema.resolve_column_name(&User::email) == "email");
    TEST_CHECK(schema.resolve_column_name(&User::username) == "username");
    TEST_CHECK(schema.resolve_column_name(&User::bio) == "bio");
    TEST_CHECK(schema.resolve_column_name(&User::balance) == "balance");
    TEST_CHECK(schema.resolve_column_name(&User::preferences) == "preferences");
    TEST_CHECK(schema.resolve_column_name(&User::last_login_ip) == "last_login_ip");
    TEST_CHECK(schema.resolve_column_name(&User::created_at) == "created_at");
    TEST_CHECK(schema.resolve_column_name(&User::updated_at) == "updated_at");

    std::cout << "  -> PASS: All member pointers resolved to exact SQL column names.\n";
}

void test_select_builder_queries() {
    std::cout << "[Test 2] Testing SelectBuilder Query Generation (PostgreSQL, MySQL, SQLite)...\n";

    // 1. Full select projection
    auto q1 = from<User>().to_sql(DatabaseDialect::PostgreSQL);
    std::cout << "  Select All SQL: " << q1.sql << "\n";
    TEST_CHECK(q1.sql == "SELECT \"id\", \"email\", \"username\", \"bio\", \"balance\", \"preferences\", \"last_login_ip\", \"created_at\", \"updated_at\" FROM \"users\";");
    TEST_CHECK(q1.params.empty());

    // 2. Custom column projection
    auto q2 = from<User>().select(&User::id, &User::email, &User::balance).to_sql(DatabaseDialect::MySQL);
    std::cout << "  Select Proj SQL: " << q2.sql << "\n";
    TEST_CHECK(q2.sql == "SELECT `id`, `email`, `balance` FROM `users`;");

    // 3. Where clause with operators, ordering, limit & offset
    auto q3 = from<User>()
        .where(&User::email, Op::Eq, "uday@aegon.dev")
        .and_where(&User::balance, Op::Gte, 100.50)
        .order_by(&User::created_at, SortOrder::Desc)
        .limit(10)
        .offset(20)
        .to_sql(DatabaseDialect::PostgreSQL);

    std::cout << "  Complex Select SQL: " << q3.sql << "\n";
    TEST_CHECK(q3.sql == "SELECT \"id\", \"email\", \"username\", \"bio\", \"balance\", \"preferences\", \"last_login_ip\", \"created_at\", \"updated_at\" FROM \"users\" WHERE \"email\" = $1 AND \"balance\" >= $2 ORDER BY \"created_at\" DESC LIMIT 10 OFFSET 20;");
    TEST_CHECK(q3.params.size() == 2);
    TEST_CHECK(q3.params[0] == "uday@aegon.dev");
    TEST_CHECK(q3.params[1] == "100.5");

    // 4. Same query in MySQL uses '?' placeholders and backticks
    auto q3_mysql = from<User>()
        .where(&User::email, Op::Eq, "uday@aegon.dev")
        .and_where(&User::balance, Op::Gte, 100.50)
        .order_by(&User::created_at, SortOrder::Desc)
        .limit(10)
        .offset(20)
        .to_sql(DatabaseDialect::MySQL);
    TEST_CHECK(q3_mysql.sql == "SELECT `id`, `email`, `username`, `bio`, `balance`, `preferences`, `last_login_ip`, `created_at`, `updated_at` FROM `users` WHERE `email` = ? AND `balance` >= ? ORDER BY `created_at` DESC LIMIT 10 OFFSET 20;");

    // 5. IN, BETWEEN, IS NULL, OR conditions
    auto q4 = from<User>()
        .where_null(&User::bio)
        .or_where(&User::username, Op::Like, "uday%")
        .where_in(&User::username, std::vector<std::string>{"alpha", "beta"})
        .where_between(&User::balance, 10, 50)
        .to_sql(DatabaseDialect::PostgreSQL);

    std::cout << "  Advanced Where SQL: " << q4.sql << "\n";
    TEST_CHECK(q4.sql.find("\"bio\" IS NULL") != std::string::npos);
    TEST_CHECK(q4.sql.find("OR \"username\" LIKE $1") != std::string::npos);
    TEST_CHECK(q4.sql.find("AND \"username\" IN ($2, $3)") != std::string::npos);
    TEST_CHECK(q4.sql.find("AND \"balance\" BETWEEN $4 AND $5") != std::string::npos);
    TEST_CHECK(q4.params.size() == 5);
    TEST_CHECK(q4.params[0] == "uday%");
    TEST_CHECK(q4.params[1] == "alpha");
    TEST_CHECK(q4.params[2] == "beta");
    TEST_CHECK(q4.params[3] == "10");
    TEST_CHECK(q4.params[4] == "50");

    std::cout << "  -> PASS: SelectBuilder generates exact multi-dialect parameterized queries.\n";
}

void test_insert_builder_and_auto_mapping() {
    std::cout << "[Test 3] Testing InsertBuilder Entity Auto-Mapping...\n";

    User u{
        .id = UUID::from_string("a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11").value(),
        .email = "uday@aegon.dev",
        .username = "uday",
        .bio = "Building Aegon",
        .balance = Decimal128::from_string("1500.5000").value(),
        .preferences = Json("{\"theme\":\"dark\"}"),
        .last_login_ip = IpAddress::from_string("192.168.1.50").value(),
        .created_at = DateTime::from_string("2026-09-07T12:00:00.000000Z").value(),
        .updated_at = DateTime::from_string("2026-09-07T12:00:00.000000Z").value()
    };

    auto ins_pg = insert_into<User>().values(u).to_sql(DatabaseDialect::PostgreSQL);
    std::cout << "  Insert PG SQL: " << ins_pg.sql << "\n";
    TEST_CHECK(ins_pg.sql == "INSERT INTO \"users\" (\"id\", \"email\", \"username\", \"bio\", \"balance\", \"preferences\", \"last_login_ip\", \"created_at\", \"updated_at\") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9);");
    TEST_CHECK(ins_pg.params.size() == 9);
    TEST_CHECK(ins_pg.params[0] == "a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11");
    TEST_CHECK(ins_pg.params[1] == "uday@aegon.dev");
    TEST_CHECK(ins_pg.params[2] == "uday");
    TEST_CHECK(ins_pg.params[3] == "Building Aegon");
    TEST_CHECK(ins_pg.params[4] == "1500.5000");
    TEST_CHECK(ins_pg.params[5] == "{\"theme\":\"dark\"}");
    TEST_CHECK(ins_pg.params[6] == "192.168.1.50");

    // Test Auto-Increment Primary Key auto-skipping and RETURNING id
    Post p{
        .id = 0,
        .author_id = u.id,
        .title = "Aegon ORM",
        .body = "C++26 Zero-Macro High-Performance ORM"
    };

    auto post_ins = insert_into<Post>().values(p).to_sql(DatabaseDialect::PostgreSQL);
    std::cout << "  Post Insert PG SQL: " << post_ins.sql << "\n";
    TEST_CHECK(post_ins.sql == "INSERT INTO \"posts\" (\"author_id\", \"title\", \"body\") VALUES ($1, $2, $3) RETURNING \"id\";");
    TEST_CHECK(post_ins.params.size() == 3);
    TEST_CHECK(post_ins.params[0] == "a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11");
    TEST_CHECK(post_ins.params[1] == "Aegon ORM");
    TEST_CHECK(post_ins.params[2] == "C++26 Zero-Macro High-Performance ORM");

    std::cout << "  -> PASS: InsertBuilder auto-maps entities and handles auto-increment IDs.\n";
}

void test_update_and_delete_builders() {
    std::cout << "[Test 4] Testing UpdateBuilder and DeleteBuilder...\n";

    UUID user_id = UUID::from_string("a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11").value();

    // Partial column updates
    auto up1 = update<User>()
        .set(&User::bio, "Updated Bio")
        .set(&User::balance, Decimal128::from_string("2000.0000").value())
        .where(&User::id, Op::Eq, user_id)
        .to_sql(DatabaseDialect::PostgreSQL);

    std::cout << "  Partial Update SQL: " << up1.sql << "\n";
    TEST_CHECK(up1.sql == "UPDATE \"users\" SET \"bio\" = $1, \"balance\" = $2 WHERE \"id\" = $3;");
    TEST_CHECK(up1.params.size() == 3);
    TEST_CHECK(up1.params[0] == "Updated Bio");
    TEST_CHECK(up1.params[1] == "2000.0000");
    TEST_CHECK(up1.params[2] == "a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11");

    // Full Entity update
    User u{
        .id = user_id,
        .email = "new_email@aegon.dev",
        .username = "new_user",
        .bio = "Brand new bio",
        .balance = Decimal128::from_string("500.0000").value(),
        .preferences = Json("{}"),
        .last_login_ip = IpAddress::from_string("127.0.0.1").value(),
        .created_at = DateTime::from_string("2026-09-07T12:00:00.000000Z").value(),
        .updated_at = DateTime::from_string("2026-09-07T12:30:00.000000Z").value()
    };

    auto up2 = update<User>()
        .set_entity(u)
        .where(&User::id, Op::Eq, user_id)
        .to_sql(DatabaseDialect::MySQL);

    std::cout << "  Entity Update MySQL SQL: " << up2.sql << "\n";
    TEST_CHECK(up2.sql.find("UPDATE `users` SET `id` = ?, `email` = ?") != std::string::npos);
    TEST_CHECK(up2.params.size() == 10); // 9 fields updated + 1 where condition

    // Delete query
    auto del = delete_from<User>()
        .where(&User::id, Op::Eq, user_id)
        .to_sql(DatabaseDialect::PostgreSQL);

    std::cout << "  Delete SQL: " << del.sql << "\n";
    TEST_CHECK(del.sql == "DELETE FROM \"users\" WHERE \"id\" = $1;");
    TEST_CHECK(del.params.size() == 1);
    TEST_CHECK(del.params[0] == "a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11");

    std::cout << "  -> PASS: UpdateBuilder and DeleteBuilder verified.\n";
}

void test_row_to_entity_auto_hydration() {
    std::cout << "[Test 5] Testing Row -> Entity Auto-Hydration (Primitives, Optionals, Foundational Types)...\n";

    // Build a mock database row matching schema of User
    MockRowView row1;
    row1.add_value("550e8400-e29b-41d4-a716-446655440000"); // id
    row1.add_value("test@aegon.dev");                       // email
    row1.add_value("aegon_user");                           // username
    row1.add_value("Loving C++26 ORM");                     // bio (optional<string>)
    row1.add_value("9876.5432");                            // balance (Decimal128)
    row1.add_value("{\"role\":\"admin\"}");                 // preferences (Json)
    row1.add_value("10.0.0.1");                             // last_login_ip (IpAddress)
    row1.add_value("2026-09-07T14:30:00.000000Z");          // created_at (DateTime)
    row1.add_value("2026-09-07T15:00:00.000000Z");          // updated_at (DateTime)

    auto select_builder = from<User>();
    User u1 = select_builder.map_row(row1);

    TEST_CHECK(u1.id.to_string() == "550e8400-e29b-41d4-a716-446655440000");
    TEST_CHECK(u1.email == "test@aegon.dev");
    TEST_CHECK(u1.username == "aegon_user");
    TEST_CHECK(u1.bio.has_value() && *u1.bio == "Loving C++26 ORM");
    TEST_CHECK(u1.balance.to_string() == "9876.5432");
    TEST_CHECK(u1.preferences.str() == "{\"role\":\"admin\"}");
    TEST_CHECK(u1.last_login_ip.to_string() == "10.0.0.1");
    TEST_CHECK(u1.created_at == DateTime::from_string("2026-09-07T14:30:00.000000Z").value());
    TEST_CHECK(u1.updated_at == DateTime::from_string("2026-09-07T15:00:00.000000Z").value());

    // Test with NULL bio
    MockRowView row2;
    row2.add_value("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
    row2.add_value("nullbio@aegon.dev");
    row2.add_value("null_bio_user");
    row2.add_null(); // NULL bio
    row2.add_value("0.0000");
    row2.add_value("{}");
    row2.add_value("127.0.0.1");
    row2.add_value("2026-09-07T10:00:00.000000Z");
    row2.add_value("2026-09-07T10:00:00.000000Z");

    User u2 = select_builder.map_row(row2);
    TEST_CHECK(!u2.bio.has_value()); // Verified std::optional<string> is nullopt
    TEST_CHECK(u2.email == "nullbio@aegon.dev");
    TEST_CHECK(u2.balance.to_string() == "0.0000");

    // Test batch hydration
    std::vector<MockRowView> rows = {row1, row2};
    std::vector<User> users = select_builder.map_rows(rows);
    TEST_CHECK(users.size() == 2);
    TEST_CHECK(users[0].username == "aegon_user");
    TEST_CHECK(users[1].username == "null_bio_user");

    std::cout << "  -> PASS: Bi-directional auto-hydration of pure C++ structs verified.\n";
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "  AEGON C++26 SQL QUERY BUILDER & AUTO-MAPPING TESTS   \n";
    std::cout << "=======================================================\n\n";

    test_member_pointer_resolution();
    test_select_builder_queries();
    test_insert_builder_and_auto_mapping();
    test_update_and_delete_builders();
    test_row_to_entity_auto_hydration();

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL SQL QUERY BUILDER TESTS PASSED! <<<         \n";
    std::cout << "=======================================================\n\n";
    return 0;
}
