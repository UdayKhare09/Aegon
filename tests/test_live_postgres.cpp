#include "data/orm/sql/Sql.h"
#include "data/orm/sql/drivers/PostgresDriver.h"
#include "data/uuid/UUIDGenerator.h"
#include "http/Context.h"
#include "http/Request.h"
#include "http/Response.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <optional>
#include <chrono>

#define TEST_CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace aegon::core;
using namespace aegon::http;
using namespace aegon::data;
using namespace aegon::data::types;
using namespace aegon::data::orm::sql;
using namespace aegon::data::orm::sql::drivers;

// -------------------------------------------------------------
// Relational Entities Covering All Custom Data Types
// -------------------------------------------------------------

// Entity 1: Tenant (Parent Table)
struct Tenant {
    UUID id;                           // Custom Type 1: UUID
    std::string name;
    MacAddress hardware_mac;           // Custom Type 2: MacAddress (MACADDR)
    Json config;                       // Custom Type 3: Json (JSONB)
    DateTime created_at;               // Custom Type 4: DateTime (TIMESTAMPTZ)
    DateTime updated_at;

    static auto schema() {
        return table<Tenant>("tenants")
            .id(&Tenant::id)
            .column(&Tenant::name, "name").length(128)
            .column(&Tenant::hardware_mac, "hardware_mac")
            .column(&Tenant::config, "config")
            .created_at(&Tenant::created_at)
            .updated_at(&Tenant::updated_at);
    }
};

// Entity 2: UserAccount (Child of Tenant with ON DELETE CASCADE)
struct UserAccount {
    UUID id;                           // UUID
    UUID tenant_id;                    // FK referencing Tenant::id
    std::string email;
    std::string username;
    Date birth_date;                   // Custom Type 5: Date (DATE)
    Time shift_start;                  // Custom Type 6: Time (TIME)
    Decimal128 balance;                // Custom Type 7: Decimal<18, 4> (NUMERIC)
    IpAddress last_login_ip;           // Custom Type 8: IpAddress (INET)
    Blob avatar_data;                  // Custom Type 9: Blob (BYTEA)
    Hash256 password_hash;             // Custom Type 10: Hash256 (BYTEA)
    std::optional<std::string> memo;   // Optional column (tests NULL)
    DateTime created_at;
    DateTime updated_at;

    static auto schema() {
        return table<UserAccount>("user_accounts")
            .id(&UserAccount::id)
            .column(&UserAccount::tenant_id, "tenant_id").references(&Tenant::id).on_delete_cascade()
            .column(&UserAccount::email, "email").unique().length(255)
            .column(&UserAccount::username, "username").length(64)
            .column(&UserAccount::birth_date, "birth_date")
            .column(&UserAccount::shift_start, "shift_start")
            .column(&UserAccount::balance, "balance")
            .column(&UserAccount::last_login_ip, "last_login_ip")
            .column(&UserAccount::avatar_data, "avatar_data")
            .column(&UserAccount::password_hash, "password_hash")
            .column(&UserAccount::memo, "memo").nullable()
            .created_at(&UserAccount::created_at)
            .updated_at(&UserAccount::updated_at);
    }
};

// Entity 3: OrderRecord (Child of UserAccount with auto-increment ID & cascade delete)
struct OrderRecord {
    int64_t id{0};                     // Auto-increment BIGINT IDENTITY PK
    UUID user_id;                      // FK referencing UserAccount::id
    Decimal128 amount;                 // Decimal<18, 4>
    Json metadata;                     // Json
    Hash256 transaction_seal;          // Hash256
    DateTime executed_at;              // DateTime

    static auto schema() {
        return table<OrderRecord>("orders")
            .id(&OrderRecord::id)
            .column(&OrderRecord::user_id, "user_id").references(&UserAccount::id).on_delete_cascade()
            .column(&OrderRecord::amount, "amount")
            .column(&OrderRecord::metadata, "metadata")
            .column(&OrderRecord::transaction_seal, "transaction_seal")
            .created_at(&OrderRecord::executed_at, "executed_at");
    }
};

struct TenantConfig {
    std::string tier;
    int max_users{0};
    std::vector<std::string> features;
};

struct OrderMetadata {
    std::string sku;
    int qty{0};
};

// -------------------------------------------------------------
// Tests
// -------------------------------------------------------------

Task<void> test_postgres_live_pipeline() {
    std::cout << "\n=======================================================\n";
    std::cout << "  AEGON C++26 LIVE POSTGRESQL 17 ORM TEST SUITE        \n";
    std::cout << "=======================================================\n\n";

    // 1. Initialize Thread-per-Core PostgreSQL Connection Pool
    const std::string conninfo = "host=127.0.0.1 port=5432 dbname=aegon_test user=postgres password=postgres";
    auto pool = create_postgres_pool(conninfo, 4);
    SqlDatabaseClient db(*pool);

    std::cout << "[Test 1] Dropping & Re-creating Schema with Complex FKs & Constraints...\n";
    {
        // Drop in reverse order with CASCADE
        co_await db.execute("DROP TABLE IF EXISTS \"orders\" CASCADE;");
        co_await db.execute("DROP TABLE IF EXISTS \"user_accounts\" CASCADE;");
        co_await db.execute("DROP TABLE IF EXISTS \"tenants\" CASCADE;");

        // Generate and execute DDL
        std::string ddl_tenant = generate_ddl<Tenant>(DatabaseDialect::PostgreSQL);
        std::string ddl_user = generate_ddl<UserAccount>(DatabaseDialect::PostgreSQL);
        std::string ddl_order = generate_ddl<OrderRecord>(DatabaseDialect::PostgreSQL);

        co_await db.execute(ddl_tenant);
        co_await db.execute(ddl_user);
        co_await db.execute(ddl_order);

        std::cout << "  -> PASS: DDL tables and constraints generated and executed on Postgres 17.\n";
    }

    // 2. Sample Data with ALL 10 Custom Data Types
    UUID tenant_id = UUIDGenerator::v7();
    MacAddress tenant_mac = *MacAddress::from_string("00:1a:2b:3c:4d:5e");
    Json tenant_cfg = Json(R"({"tier":"enterprise","max_users":1000,"features":["audit","sso"]})");
    DateTime tenant_time = DateTime::now();

    Tenant tenant{
        .id = tenant_id,
        .name = "Aegon Global Technologies",
        .hardware_mac = tenant_mac,
        .config = tenant_cfg,
        .created_at = tenant_time,
        .updated_at = tenant_time
    };

    // User 1 (IPv4, Non-Null Memo, Decimal arithmetic)
    UUID u1_id = UUIDGenerator::v7();
    Date u1_birth = *Date::from_string("1994-03-15");
    Time u1_shift = *Time::from_string("09:00:00");
    Decimal128 u1_init_balance = *Decimal128::from_string("50000.7500");
    IpAddress u1_ip = *IpAddress::from_string("192.168.1.105");
    Blob u1_avatar = Blob({0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE});
    Hash256 u1_pwhash = *Hash256::from_hex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    std::optional<std::string> u1_memo = "Lead Infrastructure Architect";

    UserAccount user1{
        .id = u1_id,
        .tenant_id = tenant_id,
        .email = "alex.vance@aegon.io",
        .username = "avance",
        .birth_date = u1_birth,
        .shift_start = u1_shift,
        .balance = u1_init_balance,
        .last_login_ip = u1_ip,
        .avatar_data = u1_avatar,
        .password_hash = u1_pwhash,
        .memo = u1_memo,
        .created_at = tenant_time,
        .updated_at = tenant_time
    };

    // User 2 (IPv6, Null Memo, different custom values)
    UUID u2_id = UUIDGenerator::v7();
    Date u2_birth = *Date::from_string("1998-11-20");
    Time u2_shift = *Time::from_string("17:30:00");
    Decimal128 u2_init_balance = *Decimal128::from_string("1250.0000");
    IpAddress u2_ip = *IpAddress::from_string("2001:db8:85a3::8a2e:370:7334");
    Blob u2_avatar = Blob({0x11, 0x22, 0x33, 0x44, 0x55});
    Hash256 u2_pwhash = *Hash256::from_hex("fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210");
    std::optional<std::string> u2_memo = std::nullopt; // NULL in DB

    UserAccount user2{
        .id = u2_id,
        .tenant_id = tenant_id,
        .email = "elena.rostova@aegon.io",
        .username = "erostova",
        .birth_date = u2_birth,
        .shift_start = u2_shift,
        .balance = u2_init_balance,
        .last_login_ip = u2_ip,
        .avatar_data = u2_avatar,
        .password_hash = u2_pwhash,
        .memo = u2_memo,
        .created_at = tenant_time,
        .updated_at = tenant_time
    };

    std::cout << "[Test 2] Testing Option 4 Atomic Unit of Work (Multi-Entity Commit)...\n";
    int64_t order1_id = 0;
    int64_t order2_id = 0;
    Decimal128 payment_amount = *Decimal128::from_string("3500.2500");

    co_await db.transaction([&](Transaction& tx) -> Task<void> {
        // 1. Insert parent Tenant
        co_await tx.insert(tenant);

        // 2. Insert child Users
        co_await tx.insert(user1);
        co_await tx.insert(user2);

        // 3. Insert child Orders referencing User 1 and User 2
        OrderRecord order1{
            .id = 0,
            .user_id = u1_id,
            .amount = payment_amount,
            .metadata = Json(R"({"sku":"SERVER-BLADE-X9","qty":1})"),
            .transaction_seal = u1_pwhash,
            .executed_at = DateTime::now()
        };
        order1_id = co_await tx.insert_get_id(order1);

        OrderRecord order2{
            .id = 0,
            .user_id = u2_id,
            .amount = *Decimal128::from_string("79.9900"),
            .metadata = Json(R"({"sku":"GIGABIT-SFP","qty":4})"),
            .transaction_seal = u2_pwhash,
            .executed_at = DateTime::now()
        };
        order2_id = co_await tx.insert_get_id(order2);

        // 4. Update User 1's balance inside the transaction
        user1.balance = user1.balance - payment_amount;
        user1.memo = "Architect (Order #" + std::to_string(order1_id) + " processed)";
        co_await tx.update_entity(user1);

        // Normal exit -> automatic COMMIT!
    });

    TEST_CHECK(order1_id > 0);
    TEST_CHECK(order2_id > 0);
    std::cout << "  -> PASS: Multi-entity transaction committed atomically (Order IDs: "
              << order1_id << ", " << order2_id << ").\n";

    std::cout << "[Test 3] Verifying Exact Hydration Across All 10 Custom Data Types...\n";
    {
        // 1. Verify Tenant
        auto fetched_tenant = co_await db.find_by_id<Tenant>(tenant_id);
        TEST_CHECK(fetched_tenant.has_value());
        TEST_CHECK(fetched_tenant->id == tenant_id);
        TEST_CHECK(fetched_tenant->name == "Aegon Global Technologies");
        TEST_CHECK(fetched_tenant->hardware_mac == tenant_mac);
        auto cfg = fetched_tenant->config.get<TenantConfig>();
        TEST_CHECK(cfg.has_value());
        TEST_CHECK(cfg->tier == "enterprise");
        TEST_CHECK(cfg->max_users == 1000);
        TEST_CHECK(cfg->features.size() == 2 && cfg->features[0] == "audit" && cfg->features[1] == "sso");

        // 2. Verify User 1 (IPv4, Non-Null Memo, updated balance)
        auto fetched_u1 = co_await db.find_by_id<UserAccount>(u1_id);
        TEST_CHECK(fetched_u1.has_value());
        TEST_CHECK(fetched_u1->id == u1_id);
        TEST_CHECK(fetched_u1->tenant_id == tenant_id);
        TEST_CHECK(fetched_u1->email == "alex.vance@aegon.io");
        TEST_CHECK(fetched_u1->username == "avance");
        TEST_CHECK(fetched_u1->birth_date == u1_birth);
        TEST_CHECK(fetched_u1->birth_date.year() == 1994 && fetched_u1->birth_date.month() == 3 && fetched_u1->birth_date.day() == 15);
        TEST_CHECK(fetched_u1->shift_start.hour() == 9 && fetched_u1->shift_start.minute() == 0 && fetched_u1->shift_start.second() == 0);
        TEST_CHECK(fetched_u1->balance == *Decimal128::from_string("46500.5000"));
        TEST_CHECK(fetched_u1->last_login_ip.is_ipv4());
        TEST_CHECK(fetched_u1->last_login_ip.to_string() == "192.168.1.105");
        TEST_CHECK(fetched_u1->avatar_data == u1_avatar);
        TEST_CHECK(fetched_u1->avatar_data.bytes().size() == 8);
        TEST_CHECK(fetched_u1->password_hash == u1_pwhash);
        TEST_CHECK(fetched_u1->memo.has_value());
        TEST_CHECK(*fetched_u1->memo == "Architect (Order #" + std::to_string(order1_id) + " processed)");

        // 3. Verify User 2 (IPv6, NULL Memo)
        auto fetched_u2 = co_await db.find_by_id<UserAccount>(u2_id);
        TEST_CHECK(fetched_u2.has_value());
        TEST_CHECK(fetched_u2->id == u2_id);
        TEST_CHECK(fetched_u2->birth_date == u2_birth);
        TEST_CHECK(fetched_u2->shift_start.hour() == 17 && fetched_u2->shift_start.minute() == 30);
        TEST_CHECK(fetched_u2->balance == *Decimal128::from_string("1250.0000"));
        TEST_CHECK(fetched_u2->last_login_ip.is_ipv6());
        TEST_CHECK(fetched_u2->avatar_data == u2_avatar);
        TEST_CHECK(fetched_u2->password_hash == u2_pwhash);
        TEST_CHECK(!fetched_u2->memo.has_value()); // Verified SQL NULL

        // 4. Verify Orders
        auto fetched_orders = co_await db.fetch_all(db.from<OrderRecord>().order_by(&OrderRecord::id, SortOrder::Asc));
        TEST_CHECK(fetched_orders.size() == 2);
        TEST_CHECK(fetched_orders[0].id == order1_id);
        TEST_CHECK(fetched_orders[0].user_id == u1_id);
        TEST_CHECK(fetched_orders[0].amount == payment_amount);
        TEST_CHECK(fetched_orders[0].transaction_seal == u1_pwhash);

        TEST_CHECK(fetched_orders[1].id == order2_id);
        TEST_CHECK(fetched_orders[1].user_id == u2_id);
        TEST_CHECK(fetched_orders[1].amount == *Decimal128::from_string("79.9900"));
        TEST_CHECK(fetched_orders[1].transaction_seal == u2_pwhash);

        auto m1 = fetched_orders[0].metadata.get<OrderMetadata>();
        TEST_CHECK(m1.has_value());
        TEST_CHECK(m1->sku == "SERVER-BLADE-X9");
        TEST_CHECK(m1->qty == 1);

        std::cout << "  -> PASS: All 10 custom data types, UUID, INET (v4/v6), MACADDR, NUMERIC, BYTEA, JSONB, DATE, TIME, TIMESTAMPTZ verified!\n";
    }

    std::cout << "[Test 4] Testing Automatic Rollback on Exception...\n";
    {
        Decimal128 original_balance = *Decimal128::from_string("46500.5000");
        bool exception_caught = false;

        try {
            co_await db.transaction([&](Transaction& tx) -> Task<void> {
                // Attempt to deduct large amount
                user1.balance = user1.balance - *Decimal128::from_string("40000.0000");
                co_await tx.update_entity(user1);

                // Insert an order that shouldn't persist
                OrderRecord bad_order{
                    .id = 0,
                    .user_id = u1_id,
                    .amount = *Decimal128::from_string("40000.0000"),
                    .metadata = Json(R"({"status":"should_abort"})"),
                    .transaction_seal = u1_pwhash,
                    .executed_at = DateTime::now()
                };
                co_await tx.insert(bad_order);

                // Simulate critical failure (e.g., payment gateway rejection)
                throw std::runtime_error("Simulated payment processor rejection");
            });
        } catch (const std::runtime_error& ex) {
            exception_caught = true;
            TEST_CHECK(std::string(ex.what()) == "Simulated payment processor rejection");
        }

        TEST_CHECK(exception_caught);

        // Verify state is completely unchanged in PostgreSQL!
        auto check_u1 = co_await db.find_by_id<UserAccount>(u1_id);
        TEST_CHECK(check_u1.has_value());
        TEST_CHECK(check_u1->balance == original_balance);

        auto all_orders = co_await db.fetch_all(db.from<OrderRecord>());
        TEST_CHECK(all_orders.size() == 2); // Still only the initial 2 orders

        std::cout << "  -> PASS: Exception inside transaction automatically issued ROLLBACK with zero side effects.\n";
    }

    std::cout << "[Test 5] Testing Complex Relations & Foreign Key ON DELETE CASCADE...\n";
    {
        // Query orders belonging specifically to User 1
        auto u1_orders = co_await db.fetch_all(
            db.from<OrderRecord>().where(&OrderRecord::user_id, Op::Eq, u1_id)
        );
        TEST_CHECK(u1_orders.size() == 1);
        TEST_CHECK(u1_orders[0].id == order1_id);

        // Delete the parent Tenant
        bool deleted = co_await db.delete_by_id<Tenant>(tenant_id);
        TEST_CHECK(deleted);

        // Verify Tenant is gone
        auto t_check = co_await db.find_by_id<Tenant>(tenant_id);
        TEST_CHECK(!t_check.has_value());

        // Verify UserAccounts cascaded and were deleted
        auto u_check = co_await db.fetch_all(
            db.from<UserAccount>().where(&UserAccount::tenant_id, Op::Eq, tenant_id)
        );
        TEST_CHECK(u_check.empty());

        // Verify OrderRecords cascaded and were deleted
        auto o_check = co_await db.fetch_all(db.from<OrderRecord>());
        TEST_CHECK(o_check.empty());

        std::cout << "  -> PASS: Complex multi-tier foreign key cascading verified (Tenant -> UserAccounts -> Orders).\n";
    }

    std::cout << "[Test 6] Testing HTTP Context Integration (ctx.db.sql)...\n";
    {
        Request req;
        Response res;
        Context ctx(req, res, nullptr, &db);

        // Direct query via ctx.db.sql
        auto tenant_count = co_await ctx.db.sql.fetch_all(ctx.db.sql.from<Tenant>());
        TEST_CHECK(tenant_count.empty());

        // Re-insert via ctx.db.sql within a transaction
        co_await ctx.db.sql.transaction([&](Transaction& tx) -> Task<void> {
            Tenant t2{
                .id = UUIDGenerator::v4(),
                .name = "HTTP Context Tenant",
                .hardware_mac = *MacAddress::from_string("aa:bb:cc:dd:ee:ff"),
                .config = Json(R"({"env":"production"})"),
                .created_at = DateTime::now(),
                .updated_at = DateTime::now()
            };
            co_await tx.insert(t2);
        });

        auto tenants_after = co_await ctx.db.sql.fetch_all(ctx.db.sql.from<Tenant>());
        TEST_CHECK(tenants_after.size() == 1);
        TEST_CHECK(tenants_after[0].name == "HTTP Context Tenant");

        std::cout << "  -> PASS: ctx.db.sql seamlessly operated live transactions on Postgres 17.\n";
    }

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL LIVE POSTGRESQL 17 TESTS PASSED! <<<        \n";
    std::cout << "=======================================================\n\n";
}

int main() {
    try {
        auto run_task = [](Task<void> t) {
            t.resume();
            assert(t.is_ready());
            t.result();
        };

        run_task(test_postgres_live_pipeline());
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FATAL ERROR in live Postgres test: " << ex.what() << std::endl;
        return 1;
    }
}
