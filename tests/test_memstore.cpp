/**
 * @file test_memstore.cpp
 * @brief Unit tests for aegon::data::memory::MemStore
 *
 * Tests run on a plain std::thread (not inside an Aegon EventLoop) so we
 * call MemStore::submit() and signal/wait the eventfd manually instead of
 * going through io_uring. This is intentional — the async io_uring wakeup
 * path is an integration detail exercised by the full server tests.
 */

#include "data/memory/MemStore.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <sys/eventfd.h>
#include <unistd.h>

using namespace aegon::data::memory;

// ─────────────────────────────────────────────────────────────────────────────
// Helper: synchronous submit + wait (no io_uring needed for unit tests)
// ─────────────────────────────────────────────────────────────────────────────
static void sync_submit(MemStore& store, MemRequest& req) {
    int efd = ::eventfd(0, 0); // blocking eventfd for tests
    assert(efd >= 0);
    req.notify_fd = efd;
    store.submit(&req);
    // Block until MemStore thread writes the result
    uint64_t val{0};
    [[maybe_unused]] auto n = ::read(efd, &val, sizeof(val));
    ::close(efd);
    req.notify_fd = -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────

static void test_set_get() {
    MemStore store;
    store.start();

    // SET
    MemRequest set_req;
    set_req.op    = MemOp::Set;
    set_req.key   = "hello";
    set_req.value = "world";
    sync_submit(store, set_req);
    assert(set_req.result_bool == true);

    // GET (hit)
    MemRequest get_req;
    get_req.op  = MemOp::Get;
    get_req.key = "hello";
    sync_submit(store, get_req);
    assert(get_req.result_str.has_value());
    assert(*get_req.result_str == "world");

    // GET (miss)
    MemRequest miss_req;
    miss_req.op  = MemOp::Get;
    miss_req.key = "missing";
    sync_submit(store, miss_req);
    assert(!miss_req.result_str.has_value());

    store.stop();
    std::cout << "[PASS] test_set_get\n";
}

static void test_del() {
    MemStore store;
    store.start();

    MemRequest set_req;
    set_req.op    = MemOp::Set;
    set_req.key   = "to_delete";
    set_req.value = "bye";
    sync_submit(store, set_req);

    MemRequest del_req;
    del_req.op  = MemOp::Del;
    del_req.key = "to_delete";
    sync_submit(store, del_req);
    assert(del_req.result_bool == true);

    // Second del should return false (not found)
    MemRequest del2_req;
    del2_req.op  = MemOp::Del;
    del2_req.key = "to_delete";
    sync_submit(store, del2_req);
    assert(del2_req.result_bool == false);

    // GET after del should be null
    MemRequest get_req;
    get_req.op  = MemOp::Get;
    get_req.key = "to_delete";
    sync_submit(store, get_req);
    assert(!get_req.result_str.has_value());

    store.stop();
    std::cout << "[PASS] test_del\n";
}

static void test_mget() {
    MemStore store;
    store.start();

    for (int i = 0; i < 5; ++i) {
        MemRequest sr;
        sr.op    = MemOp::Set;
        sr.key   = "k" + std::to_string(i);
        sr.value = "v" + std::to_string(i);
        sync_submit(store, sr);
    }

    MemRequest mget_req;
    mget_req.op   = MemOp::MGet;
    mget_req.keys = {"k0", "k2", "missing", "k4"};
    sync_submit(store, mget_req);

    assert(mget_req.result_mstr.size() == 4);
    assert(mget_req.result_mstr[0] == "v0");
    assert(mget_req.result_mstr[1] == "v2");
    assert(!mget_req.result_mstr[2].has_value());
    assert(mget_req.result_mstr[3] == "v4");

    store.stop();
    std::cout << "[PASS] test_mget\n";
}

static void test_del_many() {
    MemStore store;
    store.start();

    for (int i = 0; i < 4; ++i) {
        MemRequest sr;
        sr.op    = MemOp::Set;
        sr.key   = "dm" + std::to_string(i);
        sr.value = "val";
        sync_submit(store, sr);
    }

    MemRequest dm_req;
    dm_req.op   = MemOp::DelMany;
    dm_req.keys = {"dm0", "dm1", "dm_nonexistent"};
    sync_submit(store, dm_req);
    assert(dm_req.result_i64 == 2);

    store.stop();
    std::cout << "[PASS] test_del_many\n";
}

static void test_incr() {
    MemStore store;
    store.start();

    // Incr on new key → 1
    MemRequest r1;
    r1.op  = MemOp::Incr;
    r1.key = "counter";
    sync_submit(store, r1);
    assert(r1.result_i64 == 1);

    // Incr again → 2
    MemRequest r2;
    r2.op  = MemOp::Incr;
    r2.key = "counter";
    sync_submit(store, r2);
    assert(r2.result_i64 == 2);

    // Incr → 3
    MemRequest r3;
    r3.op  = MemOp::Incr;
    r3.key = "counter";
    sync_submit(store, r3);
    assert(r3.result_i64 == 3);

    store.stop();
    std::cout << "[PASS] test_incr\n";
}

static void test_ttl_expiry() {
    // Use a very short sweep interval so we don't sleep long
    MemStoreConfig cfg;
    cfg.sweep_interval = std::chrono::milliseconds(50);
    MemStore store(cfg);
    store.start();

    MemRequest sr;
    sr.op    = MemOp::Set;
    sr.key   = "ttl_key";
    sr.value = "ephemeral";
    sr.ttl   = std::chrono::seconds(0); // expires immediately (now + 0s)
    sync_submit(store, sr);

    // Wait long enough for sweep to run
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    MemRequest gr;
    gr.op  = MemOp::Get;
    gr.key = "ttl_key";
    sync_submit(store, gr);
    assert(!gr.result_str.has_value());

    store.stop();
    std::cout << "[PASS] test_ttl_expiry\n";
}

static void test_lru_eviction() {
    MemStoreConfig cfg;
    cfg.max_entries = 3;
    MemStore store(cfg);
    store.start();

    // Insert 3 entries (fills capacity)
    for (int i = 0; i < 3; ++i) {
        MemRequest sr;
        sr.op    = MemOp::Set;
        sr.key   = "lru" + std::to_string(i);
        sr.value = "val";
        sync_submit(store, sr);
    }

    // Touch lru0 and lru2 (lru1 becomes LRU)
    for (auto& k : {"lru0", "lru2"}) {
        MemRequest gr;
        gr.op  = MemOp::Get;
        gr.key = k;
        sync_submit(store, gr);
    }

    // Insert a 4th entry → lru1 should be evicted
    MemRequest sr4;
    sr4.op    = MemOp::Set;
    sr4.key   = "lru3";
    sr4.value = "new";
    sync_submit(store, sr4);

    // lru1 should be gone
    MemRequest check;
    check.op  = MemOp::Get;
    check.key = "lru1";
    sync_submit(store, check);
    assert(!check.result_str.has_value());

    // Others still present
    for (auto& k : {"lru0", "lru2", "lru3"}) {
        MemRequest gr;
        gr.op  = MemOp::Get;
        gr.key = k;
        sync_submit(store, gr);
        assert(gr.result_str.has_value());
    }

    store.stop();
    std::cout << "[PASS] test_lru_eviction\n";
}

static void test_concurrent_writers() {
    MemStore store;
    store.start();

    constexpr int N_THREADS = 8;
    constexpr int OPS_EACH  = 200;
    std::vector<std::thread> threads;

    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&store, t] {
            for (int i = 0; i < OPS_EACH; ++i) {
                MemRequest sr;
                sr.op    = MemOp::Set;
                sr.key   = "t" + std::to_string(t) + "_k" + std::to_string(i);
                sr.value = "v";
                sync_submit(store, sr);
            }
        });
    }
    for (auto& th : threads) th.join();

    // Verify a sample of entries
    MemRequest gr;
    gr.op  = MemOp::Get;
    gr.key = "t3_k99";
    sync_submit(store, gr);
    assert(gr.result_str.has_value());

    store.stop();
    std::cout << "[PASS] test_concurrent_writers\n";
}

static void test_stats() {
    MemStore store;
    store.start();

    MemRequest sr;
    sr.op    = MemOp::Set;
    sr.key   = "stat_key";
    sr.value = "val";
    sync_submit(store, sr);

    // Two hits
    for (int i = 0; i < 2; ++i) {
        MemRequest gr;
        gr.op  = MemOp::Get;
        gr.key = "stat_key";
        sync_submit(store, gr);
    }

    // One miss
    MemRequest mr;
    mr.op  = MemOp::Get;
    mr.key = "stat_missing";
    sync_submit(store, mr);

    assert(store.hits()   == 2);
    assert(store.misses() == 1);

    store.reset_stats();
    assert(store.hits()   == 0);
    assert(store.misses() == 0);

    store.stop();
    std::cout << "[PASS] test_stats\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    test_set_get();
    test_del();
    test_mget();
    test_del_many();
    test_incr();
    test_ttl_expiry();
    test_lru_eviction();
    test_concurrent_writers();
    test_stats();

    std::cout << "\nAll MemStore tests passed.\n";
    return 0;
}
