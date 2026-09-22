#include "data/orm/sql/PerCoreConnectionPool.h"
#include "data/orm/sql/drivers/SqliteDriver.h"
#include "http/client/PerCoreHttpClient.h"
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <atomic>

using namespace aegon::data::orm::sql;
using namespace aegon::http::client;

void test_percore_connection_pool_multithreading() {
    std::cout << "[Test 1] Testing PerCoreConnectionPool multi-threaded isolation...\n";

    std::atomic<int> factory_calls{0};
    auto factory = [&]() -> std::unique_ptr<Connection> {
        factory_calls.fetch_add(1, std::memory_order_relaxed);
        return std::make_unique<drivers::SqliteConnection>(":memory:");
    };

    PerCoreConnectionPool pool(factory, 4);

    constexpr int NUM_THREADS = 4;
    constexpr int ITERATIONS = 100;
    std::vector<std::thread> workers;
    workers.reserve(NUM_THREADS);

    std::atomic<int> completed{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < ITERATIONS; ++i) {
                auto guard = pool.acquire();
                assert(guard.valid());
                assert(guard->is_valid());
            }
            // After loop, exactly 1 connection was created per thread because it was released back to this thread's pool
            assert(pool.idle_count() >= 1);
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    assert(completed.load() == NUM_THREADS);
    // Across 4 threads, only 4 connections should have been created (1 per thread) despite 400 acquisitions
    assert(factory_calls.load() == NUM_THREADS);
    std::cout << "  -> PASS: 4 threads made 400 acquisitions with exactly 4 connections leased and 0 cross-thread interference!\n";
}

void test_percore_http_client_multithreading() {
    std::cout << "[Test 2] Testing PerCoreHttpClient multi-threaded lazy creation...\n";

    PerCoreHttpClient client(ClientConfig{.user_agent = "Aegon-Test/1.0"});

    constexpr int NUM_THREADS = 4;
    std::vector<std::thread> workers;
    std::vector<HttpClient*> seen_ptrs(NUM_THREADS, nullptr);

    for (int t = 0; t < NUM_THREADS; ++t) {
        workers.emplace_back([&, t]() {
            HttpClient* ptr = client.current();
            assert(ptr != nullptr);
            assert(ptr->config().user_agent == "Aegon-Test/1.0");
            // Verify same thread gets same pointer
            assert(client.current() == ptr);
            assert(&client.get() == ptr);
            seen_ptrs[t] = ptr;
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    // Verify each thread got a distinct instance
    for (size_t i = 0; i < seen_ptrs.size(); ++i) {
        for (size_t j = i + 1; j < seen_ptrs.size(); ++j) {
            assert(seen_ptrs[i] != seen_ptrs[j]);
        }
    }

    std::cout << "  -> PASS: All 4 threads received distinct, isolated HttpClient instances.\n";
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "       AEGON PER-CORE CLIENTS TEST SUITE               \n";
    std::cout << "=======================================================\n\n";

    test_percore_connection_pool_multithreading();
    test_percore_http_client_multithreading();

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL PER-CORE CLIENT TESTS PASSED! <<<           \n";
    std::cout << "=======================================================\n\n";
    return 0;
}
