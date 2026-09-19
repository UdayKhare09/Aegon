#include "http/ServiceRegistry.h"
#include "http/Context.h"
#include "http/Server.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <vector>

using namespace aegon::http;

struct TestDb {
    std::string name;
    explicit TestDb(std::string n) : name(std::move(n)) {}
};

struct TestCache {
    std::string host;
    int port;
    TestCache(std::string h, int p) : host(std::move(h)), port(p) {}
};

struct TestLogger {
    std::string level;
    explicit TestLogger(std::string lvl) : level(std::move(lvl)) {}
};

void test_unkeyed_service() {
    std::cout << "[TEST 1] Unkeyed Service Registration & Retrieval..." << std::endl;

    ServiceRegistry registry;
    assert(!registry.has<TestDb>());
    assert(registry.get<TestDb>() == nullptr);
    assert(registry.get_shared<TestDb>() == nullptr);

    auto db = std::make_shared<TestDb>("postgres_primary");
    registry.register_service<TestDb>(db);

    assert(registry.has<TestDb>());
    assert(registry.get<TestDb>() != nullptr);
    assert(registry.get<TestDb>()->name == "postgres_primary");
    assert(registry.require<TestDb>().name == "postgres_primary");
    assert(registry.get_shared<TestDb>() == db);

    // Missing service throws
    bool threw = false;
    try {
        (void)registry.require<TestLogger>();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);

    std::cout << "  -> PASS\n";
}

void test_keyed_named_services() {
    std::cout << "[TEST 2] Keyed / Named Services (Multiple Instances of Same Type)..." << std::endl;

    ServiceRegistry registry;

    // Register primary and replica under the same type TestDb
    registry.register_service<TestDb>("primary", std::make_shared<TestDb>("main_writer"));
    registry.register_service<TestDb>("replica", std::make_shared<TestDb>("read_replica"));

    // Also register default unkeyed db
    registry.register_service<TestDb>(std::make_shared<TestDb>("default_db"));

    assert(registry.has<TestDb>());
    assert(registry.has<TestDb>("primary"));
    assert(registry.has<TestDb>("replica"));
    assert(!registry.has<TestDb>("analytics"));

    assert(registry.get<TestDb>()->name == "default_db");
    assert(registry.get<TestDb>("primary")->name == "main_writer");
    assert(registry.get<TestDb>("replica")->name == "read_replica");
    assert(registry.get<TestDb>("analytics") == nullptr);

    assert(registry.require<TestDb>("primary").name == "main_writer");
    assert(registry.require<TestDb>("replica").name == "read_replica");

    // Missing named service throws with descriptive error
    bool threw = false;
    try {
        (void)registry.require<TestDb>("analytics");
    } catch (const std::runtime_error& ex) {
        threw = true;
        std::string msg = ex.what();
        assert(msg.find("named service 'analytics'") != std::string::npos);
    }
    assert(threw);

    std::cout << "  -> PASS\n";
}

void test_frozen_registry_zero_lock() {
    std::cout << "[TEST 3] Frozen Registry & Zero-Lock Hot Path..." << std::endl;

    ServiceRegistry registry;
    registry.register_service<TestDb>(std::make_shared<TestDb>("cluster_db"));
    registry.register_service<TestDb>("primary", std::make_shared<TestDb>("fast_primary"));
    registry.register_service<TestDb>("replica", std::make_shared<TestDb>("fast_replica"));
    registry.register_service<TestCache>("redis_cache", std::make_shared<TestCache>("127.0.0.1", 6379));

    assert(!registry.is_frozen());
    registry.freeze();
    assert(registry.is_frozen());

    // Calling freeze again is idempotent
    registry.freeze();
    assert(registry.is_frozen());

    // 1. Hot-path unkeyed lookup (O(1) direct array indexing)
    assert(registry.has<TestDb>());
    assert(registry.get<TestDb>() != nullptr);
    assert(registry.get<TestDb>()->name == "cluster_db");
    assert(registry.require<TestDb>().name == "cluster_db");

    // 2. Hot-path keyed lookups (zero-lock immutable table)
    assert(registry.has<TestDb>("primary"));
    assert(registry.require<TestDb>("primary").name == "fast_primary");
    assert(registry.require<TestDb>("replica").name == "fast_replica");
    assert(registry.require<TestCache>("redis_cache").port == 6379);

    // 3. Negative lookups on frozen registry
    assert(!registry.has<TestLogger>());
    assert(registry.get<TestLogger>() == nullptr);
    assert(!registry.has<TestDb>("nonexistent"));
    assert(registry.get<TestDb>("nonexistent") == nullptr);

    // 4. Modifying frozen registry throws std::runtime_error
    bool threw_unkeyed = false;
    try {
        registry.register_service<TestLogger>(std::make_shared<TestLogger>("debug"));
    } catch (const std::runtime_error&) {
        threw_unkeyed = true;
    }
    assert(threw_unkeyed);

    bool threw_keyed = false;
    try {
        registry.register_service<TestDb>("new_db", std::make_shared<TestDb>("late_joiner"));
    } catch (const std::runtime_error&) {
        threw_keyed = true;
    }
    assert(threw_keyed);

    std::cout << "  -> PASS\n";
}

void test_context_integration() {
    std::cout << "[TEST 4] Context Keyed & Unkeyed Service Integration..." << std::endl;

    ServiceRegistry registry;
    registry.register_service<TestDb>(std::make_shared<TestDb>("main_db"));
    registry.register_service<TestDb>("analytics", std::make_shared<TestDb>("analytics_warehouse"));
    registry.register_service<TestCache>("session_store", std::make_shared<TestCache>("redis-cluster", 7000));
    registry.freeze();

    Request req;
    Response res;
    Context ctx(req, res, &registry);

    // 1. Unkeyed retrieval via Context
    assert(ctx.has_service<TestDb>());
    assert(ctx.service<TestDb>().name == "main_db");
    assert(ctx.try_service<TestDb>() != nullptr);

    // 2. Keyed retrieval via Context
    assert(ctx.has_service<TestDb>("analytics"));
    assert(ctx.service<TestDb>("analytics").name == "analytics_warehouse");
    assert(ctx.service<TestCache>("session_store").port == 7000);

    // 3. Missing service handling in Context
    assert(!ctx.has_service<TestLogger>());
    assert(ctx.try_service<TestLogger>() == nullptr);
    assert(!ctx.has_service<TestDb>("missing_db"));
    assert(ctx.try_service<TestDb>("missing_db") == nullptr);

    bool threw = false;
    try {
        (void)ctx.service<TestDb>("missing_db");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);

    // Context without attached ServiceRegistry throws cleanly
    Context detached_ctx(req, res, nullptr);
    assert(!detached_ctx.has_service<TestDb>());
    assert(detached_ctx.try_service<TestDb>() == nullptr);
    bool threw_detached = false;
    try {
        (void)detached_ctx.service<TestDb>();
    } catch (const std::runtime_error& ex) {
        threw_detached = true;
        assert(std::string(ex.what()).find("ServiceRegistry is not attached") != std::string::npos);
    }
    assert(threw_detached);

    std::cout << "  -> PASS\n";
}

void test_server_api_integration() {
    std::cout << "[TEST 5] Server Provide Fluent API & Freeze..." << std::endl;

    Server server;
    // Provide unkeyed
    server.provide<TestDb>(std::make_shared<TestDb>("server_default_db"));
    // Provide named via shared_ptr
    server.provide<TestDb>("master", std::make_shared<TestDb>("server_master_db"));
    // Provide named in-place
    server.provide_named<TestCache>("redis_cache", "10.0.0.1", 6380);

    // Retrieve via server before freeze
    assert(server.service<TestDb>()->name == "server_default_db");
    assert(server.service<TestDb>("master")->name == "server_master_db");
    assert(server.service<TestCache>("redis_cache")->port == 6380);

    // Freeze services
    server.freeze_services();
    assert(server.services().is_frozen());

    // Retrieve via server after freeze
    assert(server.service<TestDb>()->name == "server_default_db");
    assert(server.service<TestDb>("master")->name == "server_master_db");
    assert(server.service<TestCache>("redis_cache")->port == 6380);

    std::cout << "  -> PASS\n";
}

void test_concurrent_multithreaded_hot_path() {
    std::cout << "[TEST 6] Concurrent Multithreaded Zero-Lock Hot Path Stress..." << std::endl;

    ServiceRegistry registry;
    registry.register_service<TestDb>(std::make_shared<TestDb>("stress_db"));
    registry.register_service<TestDb>("writer", std::make_shared<TestDb>("stress_writer"));
    registry.register_service<TestDb>("reader", std::make_shared<TestDb>("stress_reader"));
    registry.register_service<TestCache>("cache", std::make_shared<TestCache>("localhost", 6379));
    registry.freeze();

    constexpr size_t THREADS = 16;
    constexpr size_t ITERATIONS = 100000;

    std::vector<std::thread> workers;
    workers.reserve(THREADS);

    std::atomic<bool> start_gate{false};
    std::atomic<size_t> success_count{0};

    for (size_t t = 0; t < THREADS; ++t) {
        workers.emplace_back([&, t]() {
            while (!start_gate.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            Request req;
            Response res;
            Context ctx(req, res, &registry);

            size_t local_ok = 0;
            for (size_t i = 0; i < ITERATIONS; ++i) {
                // Direct unkeyed hot-path read
                auto* db0 = ctx.try_service<TestDb>();
                if (db0 && db0->name == "stress_db") local_ok++;

                // Keyed hot-path read
                auto* db_w = ctx.try_service<TestDb>("writer");
                if (db_w && db_w->name == "stress_writer") local_ok++;

                auto* db_r = ctx.try_service<TestDb>("reader");
                if (db_r && db_r->name == "stress_reader") local_ok++;

                auto* cache = ctx.try_service<TestCache>("cache");
                if (cache && cache->port == 6379) local_ok++;
            }
            success_count.fetch_add(local_ok, std::memory_order_relaxed);
        });
    }

    start_gate.store(true, std::memory_order_release);
    for (auto& w : workers) {
        w.join();
    }

    constexpr size_t EXPECTED_TOTAL = THREADS * ITERATIONS * 4;
    assert(success_count.load() == EXPECTED_TOTAL);
    std::cout << "  -> PASS (" << success_count.load() << " zero-lock lookups across " 
              << THREADS << " concurrent threads)\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "    Aegon ServiceRegistry Test Suite\n";
    std::cout << "========================================\n";

    test_unkeyed_service();
    test_keyed_named_services();
    test_frozen_registry_zero_lock();
    test_context_integration();
    test_server_api_integration();
    test_concurrent_multithreaded_hot_path();

    std::cout << "\nALL SERVICE REGISTRY TESTS PASSED!\n";
    return 0;
}
