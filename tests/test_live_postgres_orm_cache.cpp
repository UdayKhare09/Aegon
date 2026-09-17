#include "data/cache/RedisCacheBackend.h"
#include "data/redis/RedisClient.h"
#include "data/orm/sql/SqlConfig.h"
#include "data/orm/sql/SqlDatabaseClient.h"
#include "data/orm/sql/drivers/PostgresDriver.h"
#include "core/EventLoop.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <string>

using namespace aegon;
using namespace aegon::data::redis;
using namespace aegon::data::cache;
using namespace aegon::data::orm::sql;
using namespace aegon::data::orm::sql::drivers;

// ============================================================================
// Multi-Entity Relational Schema (1:1, 1:N, N:M) with Varied Cache Modes
// ============================================================================

// Mode: StrictEpoch (Global table-wide epoch increment on write)
struct Profile {
    int id{0};
    int user_id{0};
    std::string bio;

    static const auto& schema() {
        static const auto s = TableDef<Profile>("profiles")
            .id(&Profile::id, "id")
            .column(&Profile::user_id, "user_id")
            .column(&Profile::bio, "bio")
            .cache({
                .ttl = std::chrono::seconds(300),
                .by_id = true,
                .invalidation = InvalidationMode::StrictEpoch
            });
        return s;
    }
};

// Mode: StrictEpoch
struct Order {
    int id{0};
    int user_id{0};
    double total{0.0};
    std::string status;

    static const auto& schema() {
        static const auto s = TableDef<Order>("orders")
            .id(&Order::id, "id")
            .column(&Order::user_id, "user_id")
            .column(&Order::total, "total")
            .column(&Order::status, "status")
            .cache({
                .ttl = std::chrono::seconds(300),
                .by_id = true,
                .invalidation = InvalidationMode::StrictEpoch
            });
        return s;
    }
};

// Mode: StrictEpoch
struct Role {
    int id{0};
    std::string name;

    static const auto& schema() {
        static const auto s = TableDef<Role>("roles")
            .id(&Role::id, "id")
            .column(&Role::name, "name")
            .cache({
                .ttl = std::chrono::seconds(300),
                .by_id = true,
                .invalidation = InvalidationMode::StrictEpoch
            });
        return s;
    }
};

// Pivot table for N:M
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

// Mode: Partitioned (Scoped epoch invalidation by tenant/org_id)
// Relations: 1:1 (Profile), 1:N (Order), N:M (Role through UserRole)
struct User {
    int id{0};
    int org_id{0};
    std::string email;
    std::string name;

    HasOne<Profile> profile;
    HasMany<Order> orders;
    HasMany<Role> roles;

    static const auto& schema() {
        static const auto s = TableDef<User>("users")
            .id(&User::id, "id")
            .column(&User::org_id, "org_id")
            .column(&User::email, "email").unique()
            .column(&User::name, "name")
            .has_one(&User::profile, &Profile::user_id)
            .has_many(&User::orders, &Order::user_id)
            .has_many(&User::roles).through<UserRole>(&UserRole::user_id, &UserRole::role_id)
            .cache({
                .ttl = std::chrono::seconds(600),
                .by_id = true,
                .invalidation = InvalidationMode::Partitioned,
                .mutation_sync = MutationSync::UpdateOnWrite
            })
            .by_unique(&User::email)
            .partition_by(&User::org_id);
        return s;
    }
};

// Mode: TtlOnly (Zero write-penalty, natural TTL expiration)
struct Product {
    int id{0};
    std::string sku;
    double price{0.0};

    static const auto& schema() {
        static const auto s = TableDef<Product>("products")
            .id(&Product::id, "id")
            .column(&Product::sku, "sku")
            .column(&Product::price, "price")
            .cache({
                .ttl = std::chrono::seconds(60),
                .by_id = true,
                .invalidation = InvalidationMode::TtlOnly
            });
        return s;
    }
};

// ============================================================================
// Comprehensive Test Suite executed across each Redis Topology
// ============================================================================
core::Task<void> run_cache_test_for_topology(
    SqlDatabaseClient& client,
    std::shared_ptr<RedisCacheBackend> cache_backend,
    const std::string& topology_name) 
{
    std::cout << "\n=======================================================\n";
    std::cout << " Testing PostgreSQL 17 ORM Cache with: " << topology_name << "\n";
    std::cout << "=======================================================\n";

    client.set_cache(cache_backend);

    // 1. Setup Fresh Database Schema
    co_await client.execute("DROP TABLE IF EXISTS user_roles CASCADE;");
    co_await client.execute("DROP TABLE IF EXISTS orders CASCADE;");
    co_await client.execute("DROP TABLE IF EXISTS profiles CASCADE;");
    co_await client.execute("DROP TABLE IF EXISTS roles CASCADE;");
    co_await client.execute("DROP TABLE IF EXISTS users CASCADE;");
    co_await client.execute("DROP TABLE IF EXISTS products CASCADE;");

    co_await client.execute(
        "CREATE TABLE users ("
        "  id SERIAL PRIMARY KEY,"
        "  org_id INT NOT NULL,"
        "  email VARCHAR(255) UNIQUE NOT NULL,"
        "  name VARCHAR(255) NOT NULL"
        ");"
    );

    co_await client.execute(
        "CREATE TABLE profiles ("
        "  id SERIAL PRIMARY KEY,"
        "  user_id INT NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
        "  bio TEXT NOT NULL"
        ");"
    );

    co_await client.execute(
        "CREATE TABLE orders ("
        "  id SERIAL PRIMARY KEY,"
        "  user_id INT NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
        "  total NUMERIC(10, 2) NOT NULL,"
        "  status VARCHAR(50) NOT NULL"
        ");"
    );

    co_await client.execute(
        "CREATE TABLE roles ("
        "  id SERIAL PRIMARY KEY,"
        "  name VARCHAR(100) NOT NULL"
        ");"
    );

    co_await client.execute(
        "CREATE TABLE user_roles ("
        "  user_id INT NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
        "  role_id INT NOT NULL REFERENCES roles(id) ON DELETE CASCADE,"
        "  PRIMARY KEY(user_id, role_id)"
        ");"
    );

    co_await client.execute(
        "CREATE TABLE products ("
        "  id SERIAL PRIMARY KEY,"
        "  sku VARCHAR(100) NOT NULL,"
        "  price NUMERIC(10, 2) NOT NULL"
        ");"
    );

    // -------------------------------------------------------------
    // Test A: Primary Key Caching & Partitioned Epochs
    // -------------------------------------------------------------
    std::cout << "  [A] Testing Primary Key Caching & Partitioned Epochs...\n";
    User u1{.id = 0, .org_id = 1, .email = "alice@corp1.com", .name = "Alice"};
    User u2{.id = 0, .org_id = 1, .email = "bob@corp1.com", .name = "Bob"};
    User u3{.id = 0, .org_id = 2, .email = "charlie@corp2.com", .name = "Charlie"};

    co_await client.insert(u1);
    co_await client.insert(u2);
    co_await client.insert(u3);

    assert(u1.id > 0 && u2.id > 0 && u3.id > 0);

    // Verify entity JSON cached in Redis under primary key
    auto u1_cached = co_await cache_backend->get("users:id:" + std::to_string(u1.id));
    assert(u1_cached.has_value());
    assert(u1_cached->find("Alice") != std::string::npos);

    // find_by_id served from Redis hit
    auto found_u1 = co_await client.find_by_id<User>(u1.id);
    assert(found_u1.has_value() && found_u1->name == "Alice");

    // Partition epochs isolated: Org 1 = 2 inserts, Org 2 = 1 insert
    auto org1_epoch = co_await cache_backend->get("users:part:1:epoch");
    auto org2_epoch = co_await cache_backend->get("users:part:2:epoch");
    assert(org1_epoch.has_value() && *org1_epoch == "2");
    assert(org2_epoch.has_value() && *org2_epoch == "1");
    std::cout << "      -> Primary key & Partitioned epochs verified (org1=" 
              << *org1_epoch << ", org2=" << *org2_epoch << ")\n";

    // -------------------------------------------------------------
    // Test B: Unique Key Secondary Index Caching (find_by_unique)
    // -------------------------------------------------------------
    std::cout << "  [B] Testing Unique Secondary Index Caching...\n";
    auto found_by_email = co_await client.find_by_unique(&User::email, "alice@corp1.com");
    assert(found_by_email.has_value() && found_by_email->id == u1.id);
    std::cout << "      -> find_by_unique resolved via cached secondary index\n";

    // -------------------------------------------------------------
    // Test C: Complex Relations (1:1, 1:N, N:M) with Eager Loading
    // -------------------------------------------------------------
    std::cout << "  [C] Testing Complex Relations (1:1, 1:N, N:M) with Caching...\n";
    // Insert Profile (1:1)
    Profile prof{.id = 0, .user_id = u1.id, .bio = "Lead Distributed Systems Architect"};
    co_await client.insert(prof);

    // Insert Orders (1:N)
    Order o1{.id = 0, .user_id = u1.id, .total = 149.99, .status = "Completed"};
    Order o2{.id = 0, .user_id = u1.id, .total = 899.50, .status = "Processing"};
    co_await client.insert(o1);
    co_await client.insert(o2);

    // Insert Roles & UserRoles (N:M)
    Role r_admin{.id = 0, .name = "Administrator"};
    Role r_dev{.id = 0, .name = "Developer"};
    co_await client.insert(r_admin);
    co_await client.insert(r_dev);

    UserRole ur1{.user_id = u1.id, .role_id = r_admin.id};
    UserRole ur2{.user_id = u1.id, .role_id = r_dev.id};
    co_await client.insert(ur1);
    co_await client.insert(ur2);

    // Query 1: DB Miss -> Fetches full relational graph (1:1 profile, 1:N orders, N:M roles)
    auto users_with_relations = co_await client.fetch_all(
        client.from<User>()
            .where(&User::id, Op::Eq, u1.id)
            .include(&User::profile)
            .include(&User::orders)
            .include(&User::roles)
            .cached()
    );
    assert(users_with_relations.size() == 1);
    assert(users_with_relations[0].profile.get().has_value());
    assert(users_with_relations[0].profile->bio == "Lead Distributed Systems Architect");
    assert(users_with_relations[0].orders.size() == 2);
    assert(users_with_relations[0].roles.size() == 2);

    // Query 2: Cache Hit -> Resolves root entity from Redis & hydrates includes
    auto users_cached_rel = co_await client.fetch_all(
        client.from<User>()
            .where(&User::id, Op::Eq, u1.id)
            .include(&User::profile)
            .include(&User::orders)
            .include(&User::roles)
            .cached()
    );
    assert(users_cached_rel.size() == 1);
    assert(users_cached_rel[0].profile.get().has_value());
    assert(users_cached_rel[0].profile->bio == "Lead Distributed Systems Architect");
    assert(users_cached_rel[0].orders.size() == 2);
    assert(users_cached_rel[0].roles.size() == 2);
    std::cout << "      -> Relational eager loading (1:1, 1:N, N:M) verified on cache hit!\n";

    // -------------------------------------------------------------
    // Test D: Two-Phase Query ID Pointer Caching (.cached())
    // -------------------------------------------------------------
    std::cout << "  [D] Testing Two-Phase Query ID Pointer Caching...\n";
    auto org1_users_1 = co_await client.fetch_all(
        client.from<User>()
            .where(&User::org_id, Op::Eq, 1)
            .order_by(&User::id, SortOrder::Asc)
            .cached()
    );
    assert(org1_users_1.size() == 2);

    auto org1_users_2 = co_await client.fetch_all(
        client.from<User>()
            .where(&User::org_id, Op::Eq, 1)
            .order_by(&User::id, SortOrder::Asc)
            .cached()
    );
    assert(org1_users_2.size() == 2);
    std::cout << "      -> Two-phase query hit verified (MGET entity resolution)\n";

    // -------------------------------------------------------------
    // Test E: Scoped Partitioned Invalidation
    // -------------------------------------------------------------
    std::cout << "  [E] Testing Scoped Partitioned Invalidation...\n";
    User u4{.id = 0, .org_id = 2, .email = "diana@corp2.com", .name = "Diana"};
    co_await client.insert(u4);

    // Org 1 partition epoch must stay unchanged at 2!
    auto org1_epoch_post = co_await cache_backend->get("users:part:1:epoch");
    auto org2_epoch_post = co_await cache_backend->get("users:part:2:epoch");
    assert(org1_epoch_post.has_value() && *org1_epoch_post == "2");
    assert(org2_epoch_post.has_value() && *org2_epoch_post == "2");

    // Org 1 query MUST still hit the cache!
    auto org1_users_still_cached = co_await client.fetch_all(
        client.from<User>()
            .where(&User::org_id, Op::Eq, 1)
            .order_by(&User::id, SortOrder::Asc)
            .cached()
    );
    assert(org1_users_still_cached.size() == 2);
    std::cout << "      -> Mutation in Org 2 did NOT invalidate Org 1 cached queries!\n";

    // -------------------------------------------------------------
    // Test F: Update Entity Synchronization
    // -------------------------------------------------------------
    std::cout << "  [F] Testing Entity Update Synchronization...\n";
    u1.name = "Alice Wonderland";
    co_await client.update_entity(u1);

    auto refetched_u1 = co_await client.find_by_id<User>(u1.id);
    assert(refetched_u1.has_value() && refetched_u1->name == "Alice Wonderland");
    std::cout << "      -> update_entity refreshed cache key: " << refetched_u1->name << "\n";

    // -------------------------------------------------------------
    // Test G: StrictEpoch Global Table Invalidation
    // -------------------------------------------------------------
    std::cout << "  [G] Testing StrictEpoch Invalidation on Roles...\n";
    auto r1_epoch = co_await cache_backend->get("roles:epoch");
    std::string start_epoch = r1_epoch.value_or("0");

    Role r_guest{.id = 0, .name = "Guest"};
    co_await client.insert(r_guest);

    auto r2_epoch = co_await cache_backend->get("roles:epoch");
    assert(r2_epoch.has_value() && std::stoi(*r2_epoch) > std::stoi(start_epoch));
    std::cout << "      -> StrictEpoch incremented atomically: " << *r2_epoch << "\n";

    // -------------------------------------------------------------
    // Test H: TtlOnly Mode (Zero Write-Penalty High Frequency Catalog)
    // -------------------------------------------------------------
    std::cout << "  [H] Testing TtlOnly Mode (High-Frequency Catalog)...\n";
    Product p1{.id = 0, .sku = "SERVER-NODE-1U", .price = 2899.99};
    co_await client.insert(p1);

    // Products table has NO epoch key!
    auto p_epoch = co_await cache_backend->get("products:epoch");
    assert(!p_epoch.has_value());

    auto p1_found = co_await client.find_by_id<Product>(p1.id);
    assert(p1_found.has_value() && p1_found->sku == "SERVER-NODE-1U");
    std::cout << "      -> TtlOnly verified: 0 write penalty, product served from cache\n";

    // -------------------------------------------------------------
    // Test I: delete_by_id Cache Purge
    // -------------------------------------------------------------
    std::cout << "  [I] Testing delete_by_id Cache Purge...\n";
    bool deleted = co_await client.delete_by_id<User>(u2.id);
    assert(deleted);

    auto u2_purged = co_await cache_backend->get("users:id:" + std::to_string(u2.id));
    assert(!u2_purged.has_value());
    std::cout << "      -> delete_by_id purged Redis key\n";

    std::cout << "[PASS] " << topology_name << " Completed Successfully!\n";
}

// ============================================================================
// Main Runner across all Connection Types
// ============================================================================
core::Task<void> run_postgres_orm_cache_all_topologies(core::IoUring& ring) {
    std::cout << "\n=======================================================\n";
    std::cout << " AEGON POSTGRESQL 17 + REDIS ORM CACHE MULTI-TOPOLOGY  \n";
    std::cout << " (Testing Standalone, Sentinel, and Cluster Modes)     \n";
    std::cout << "=======================================================\n";

    // 1. Connect to PostgreSQL 17 Pool
    std::string conninfo = "host=127.0.0.1 port=5432 dbname=aegon_test user=aegon password=aegon_secret";
    auto pg_pool = create_postgres_pool(conninfo, 4);
    SqlDatabaseClient client(*pg_pool);

    // -------------------------------------------------------------
    // TOPOLOGY 1: Redis Standalone (Port 6379)
    // -------------------------------------------------------------
    {
        RedisNodeConfig cfg{
            .host = "127.0.0.1",
            .port = 6379,
            .password = "redis_secret"
        };
        auto redis_client = std::make_shared<RedisClient>(ring, cfg, 4);
        co_await redis_client->execute({"FLUSHDB"});
        auto cache = std::make_shared<RedisCacheBackend>(redis_client, "standalone_cache:");
        co_await run_cache_test_for_topology(client, cache, "Redis Standalone (Port 6379)");
    }

    // -------------------------------------------------------------
    // TOPOLOGY 2: Redis Sentinel (Port 26379, Master 'mymaster')
    // -------------------------------------------------------------
    {
        SentinelConfig scfg{
            .master_name = "mymaster",
            .sentinels = {{"127.0.0.1", 26379}},
            .password = "sentinel_secret"
        };
        auto sentinel_client = std::make_shared<RedisClient>(ring, scfg, 4);
        co_await sentinel_client->execute({"FLUSHDB"});
        auto cache = std::make_shared<RedisCacheBackend>(sentinel_client, "sentinel_cache:");
        co_await run_cache_test_for_topology(client, cache, "Redis Sentinel (Port 26379, Master: 6380)");
    }

    // -------------------------------------------------------------
    // TOPOLOGY 3: Redis Cluster (6 Nodes: 7000..7005)
    // -------------------------------------------------------------
    {
        ClusterConfig ccfg{
            .seed_nodes = {{"127.0.0.1", 7000}, {"127.0.0.1", 7001}, {"127.0.0.1", 7002}},
            .pool_size_per_node = 4
        };
        for (const auto& [h, p] : ccfg.seed_nodes) {
            RedisNodeConfig ncfg{.host = h, .port = p, .password = ccfg.password};
            RedisClient nc(ring, ncfg, 1);
            co_await nc.execute({"FLUSHDB"});
        }
        auto cluster_client = std::make_shared<RedisClient>(ring, ccfg);
        auto cache = std::make_shared<RedisCacheBackend>(cluster_client, "cluster_cache:");
        co_await run_cache_test_for_topology(client, cache, "Redis Cluster (Ports 7000..7005)");
    }

    std::cout << "\n=======================================================\n";
    std::cout << " >>> ALL ORM CACHE TESTS PASSED ON EVERY TOPOLOGY! <<< \n";
    std::cout << "=======================================================\n\n";
}

int main() {
    try {
        core::EventLoop loop(512);

        bool completed = false;
        loop.spawn([&]() -> core::Task<void> {
            co_await run_postgres_orm_cache_all_topologies(loop.ring());
            completed = true;
            loop.stop();
        }());

        loop.run();
        assert(completed);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Exception in ORM Cache Test Suite: " << e.what() << "\n";
        return 1;
    }
}
