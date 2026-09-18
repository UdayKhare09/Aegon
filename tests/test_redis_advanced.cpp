#include "data/redis/RedisClient.h"
#include "data/redis/RedisLock.h"
#include "data/redis/RedisPipeline.h"
#include "core/IoUring.h"
#include "core/EventLoop.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

using namespace aegon;
using namespace aegon::data::redis;

core::Task<void> test_lua_scripting(RedisClient& client) {
    std::cout << "\n[1] Testing Redis Lua Scripting...\n";

    // 1. Basic eval
    auto resp = co_await client.eval("return 42");
    assert(resp.is_integer() && resp.as_integer() == 42);
    std::cout << "  -> Basic eval returned integer: " << resp.as_integer() << "\n";

    // 2. Eval with KEYS and ARGS
    resp = co_await client.eval(
        "return {KEYS[1], ARGV[1], ARGV[2]}",
        {"lua:test:key"},
        {"val1", "val2"}
    );
    assert(resp.is_array());
    const auto& arr = resp.as_array();
    assert(arr.size() == 3);
    (void)arr;
    assert(arr[0].as_string() == "lua:test:key");
    assert(arr[1].as_string() == "val1");
    assert(arr[2].as_string() == "val2");
    std::cout << "  -> eval with KEYS and ARGV verified\n";

    // 3. SCRIPT LOAD and EVALSHA
    std::string script = "return redis.call('SET', KEYS[1], ARGV[1])";
    std::string sha = co_await client.script_load(script);
    assert(!sha.empty());
    std::cout << "  -> SCRIPT LOAD returned SHA: " << sha << "\n";

    resp = co_await client.evalsha(sha, {"lua:loaded:key"}, {"hello_sha"});
    assert(!resp.is_error());
    auto val = co_await client.get("lua:loaded:key");
    assert(val.has_value() && *val == "hello_sha");
    std::cout << "  -> EVALSHA executed successfully, verified GET: " << *val << "\n";

    // 4. eval_script automatic SHA1 computation and NOSCRIPT fallback
    std::string auto_script = "return redis.call('INCR', KEYS[1])";
    resp = co_await client.eval_script(auto_script, {"lua:counter"});
    assert(resp.is_integer() && resp.as_integer() == 1);
    resp = co_await client.eval_script(auto_script, {"lua:counter"});
    assert(resp.is_integer() && resp.as_integer() == 2);
    std::cout << "  -> eval_script verified atomic increment to: " << resp.as_integer() << "\n";
}

core::Task<void> test_distributed_locking(RedisClient& client) {
    std::cout << "\n[2] Testing Distributed Lock (RedisLock)...\n";
    std::string lock_key = "lock:orders:1001";

    // 1. Acquire lock
    auto opt_lock = co_await client.lock(lock_key, std::chrono::milliseconds(2000));
    assert(opt_lock.has_value());
    assert(opt_lock->locked());
    assert(opt_lock->key() == lock_key);
    assert(!opt_lock->token().empty());
    std::cout << "  -> Acquired lock with token: " << opt_lock->token() << "\n";

    // 2. Second acquire on same key fails
    auto opt_lock2 = co_await client.lock(lock_key, std::chrono::milliseconds(2000));
    assert(!opt_lock2.has_value());
    std::cout << "  -> Second lock acquisition correctly rejected (mutual exclusion)\n";

    // 3. Extend lock TTL
    bool extended = co_await opt_lock->extend(std::chrono::milliseconds(5000));
    assert(extended);
    (void)extended;
    int64_t remaining_ttl = co_await client.pttl(lock_key);
    assert(remaining_ttl > 2000 && remaining_ttl <= 5000);
    std::cout << "  -> Lock extended successfully, remaining PTTL: " << remaining_ttl << "ms\n";

    // 4. Release lock
    bool released = co_await opt_lock->release();
    assert(released);
    (void)released;
    assert(!opt_lock->locked());
    bool exists = co_await client.exists(lock_key);
    assert(!exists);
    (void)exists;
    std::cout << "  -> Lock released atomically via Lua script, key removed\n";

    // 5. Subsequent acquire succeeds
    auto opt_lock3 = co_await client.lock(lock_key, std::chrono::milliseconds(1000));
    assert(opt_lock3.has_value());
    assert(opt_lock3->locked());
    co_await opt_lock3->release();
    std::cout << "  -> Lock re-acquired and released cleanly\n";
}

core::Task<void> test_sorted_set_ranges(RedisClient& client) {
    std::cout << "\n[3] Testing Sorted Set Range & Inspection APIs...\n";
    std::string zkey = "leaderboard:gamers";
    co_await client.del(zkey);

    // 1. ZADD multiple elements
    assert(co_await client.zadd(zkey, "alice", 100.0));
    assert(co_await client.zadd(zkey, "bob", 200.0));
    assert(co_await client.zadd(zkey, "charlie", 300.0));
    assert(co_await client.zadd(zkey, "diana", 400.0));
    std::cout << "  -> Inserted 4 members with scores 100, 200, 300, 400\n";

    // 2. ZCARD & ZCOUNT
    int64_t card = co_await client.zcard(zkey);
    assert(card == 4);
    int64_t count = co_await client.zcount(zkey, "150", "350");
    assert(count == 2); // bob(200), charlie(300)
    std::cout << "  -> ZCARD: " << card << ", ZCOUNT [150, 350]: " << count << "\n";

    // 3. ZSCORE, ZRANK, ZREVRANK
    auto score = co_await client.zscore(zkey, "charlie");
    assert(score.has_value() && *score == 300.0);
    auto rank = co_await client.zrank(zkey, "charlie");
    assert(rank.has_value() && *rank == 2); // 0-indexed: alice(0), bob(1), charlie(2)
    auto revrank = co_await client.zrevrank(zkey, "charlie");
    assert(revrank.has_value() && *revrank == 1); // diana(0), charlie(1)
    std::cout << "  -> Charlie score: " << *score << ", rank: " << *rank << ", revrank: " << *revrank << "\n";

    // 4. ZRANGE & ZREVRANGE
    auto asc = co_await client.zrange(zkey, 0, -1);
    assert(asc.size() == 4);
    assert(asc[0] == "alice" && asc[1] == "bob" && asc[2] == "charlie" && asc[3] == "diana");

    auto desc = co_await client.zrevrange(zkey, 0, -1);
    assert(desc.size() == 4);
    assert(desc[0] == "diana" && desc[1] == "charlie" && desc[2] == "bob" && desc[3] == "alice");
    std::cout << "  -> ZRANGE and ZREVRANGE ordering verified\n";

    // 5. ZRANGE with scores
    auto asc_scores = co_await client.zrange_with_scores(zkey, 0, 1);
    assert(asc_scores.size() == 2);
    assert(asc_scores[0].first == "alice" && asc_scores[0].second == 100.0);
    assert(asc_scores[1].first == "bob" && asc_scores[1].second == 200.0);

    // 6. ZRANGEBYSCORE
    auto byscore = co_await client.zrangebyscore(zkey, "150", "350");
    assert(byscore.size() == 2);
    assert(byscore[0] == "bob" && byscore[1] == "charlie");

    auto byscore_scores = co_await client.zrangebyscore_with_scores(zkey, "150", "350");
    assert(byscore_scores.size() == 2);
    assert(byscore_scores[0].first == "bob" && byscore_scores[0].second == 200.0);
    assert(byscore_scores[1].first == "charlie" && byscore_scores[1].second == 300.0);
    std::cout << "  -> ZRANGEBYSCORE with scores verified: bob and charlie\n";
}

core::Task<void> test_streams(RedisClient& client) {
    std::cout << "\n[4] Testing Redis Streams API...\n";
    std::string stream_key = "stream:telemetry";
    co_await client.del(stream_key);

    // 1. XADD
    std::string id1 = co_await client.xadd(stream_key, "*", {{"temp", "24.5"}, {"humidity", "55"}});
    assert(!id1.empty());
    std::string id2 = co_await client.xadd(stream_key, "*", {{"temp", "25.0"}, {"humidity", "56"}});
    assert(!id2.empty());
    std::string id3 = co_await client.xadd(stream_key, "*", {{"temp", "25.5"}, {"humidity", "57"}});
    assert(!id3.empty());
    std::cout << "  -> XADD added 3 events, last ID: " << id3 << "\n";

    // 2. XLEN
    int64_t len = co_await client.xlen(stream_key);
    assert(len == 3);
    std::cout << "  -> XLEN verified: " << len << "\n";

    // 3. XRANGE
    auto range_msgs = co_await client.xrange(stream_key, "-", "+");
    assert(range_msgs.size() == 3);
    assert(range_msgs[0].id == id1);
    assert(range_msgs[0].fields.size() == 2);
    assert(range_msgs[0].fields[0].first == "temp" && range_msgs[0].fields[0].second == "24.5");
    std::cout << "  -> XRANGE retrieved 3 messages with correct fields\n";

    // 4. XREVRANGE with count
    auto rev_msgs = co_await client.xrevrange(stream_key, "+", "-", 1);
    assert(rev_msgs.size() == 1);
    assert(rev_msgs[0].id == id3);
    std::cout << "  -> XREVRANGE with LIMIT 1 retrieved latest event\n";

    // 5. XREAD
    auto read_res = co_await client.xread({stream_key}, {"0-0"}, 2);
    assert(read_res.size() == 1);
    assert(read_res[0].stream == stream_key);
    assert(read_res[0].messages.size() == 2);
    std::cout << "  -> XREAD retrieved 2 messages from stream\n";

    // 6. Consumer Groups: XGROUP CREATE & XREADGROUP & XACK
    std::string group_name = "processing_workers";
    bool grp_created = co_await client.xgroup_create(stream_key, group_name, "0", false);
    assert(grp_created);
    (void)grp_created;
    std::cout << "  -> Consumer group created: " << group_name << "\n";

    auto grp_read = co_await client.xreadgroup(group_name, "worker-1", {stream_key}, {">"}, 2);
    assert(grp_read.size() == 1);
    assert(grp_read[0].messages.size() == 2);
    std::cout << "  -> XREADGROUP worker-1 received 2 messages\n";

    // 7. XACK
    int64_t acked = co_await client.xack(stream_key, group_name, {id1, id2});
    assert(acked == 2);
    std::cout << "  -> XACK acknowledged " << acked << " messages\n";

    // 8. XDEL
    int64_t deleted = co_await client.xdel(stream_key, {id1});
    assert(deleted == 1);
    (void)deleted;
    int64_t len_after_del = co_await client.xlen(stream_key);
    assert(len_after_del == 2);
    std::cout << "  -> XDEL removed message from stream, remaining: " << len_after_del << "\n";
}

core::Task<void> test_cluster_pipeline(core::IoUring& ring) {
    std::cout << "\n[5] Testing Redis Cluster-Aware Pipeline...\n";

    // Proves that only a SINGLE seed node is needed: the router dynamically discovers
    // all remaining cluster nodes (7001, 7002...) via CLUSTER SLOTS.
    ClusterConfig cluster_cfg{
        .seed_nodes = {{"127.0.0.1", 7000}},
        .pool_size_per_node = 4
    };

    RedisClient cluster_client(ring, cluster_cfg);

    // Build pipeline spanning keys that hash to different slots
    auto pipe = cluster_client.pipeline();
    pipe.set("cluster:pipe:user:1", "Alice");
    pipe.set("cluster:pipe:order:100", "Order#100");
    pipe.set("cluster:pipe:item:500", "Widget");
    pipe.get("cluster:pipe:user:1");
    pipe.get("cluster:pipe:order:100");
    pipe.get("cluster:pipe:item:500");

    auto results = co_await pipe.execute();
    assert(results.size() == 6);

    // SET results
    assert(results[0].is_string());
    assert(results[1].is_string());
    assert(results[2].is_string());

    // GET results in preserved order
    assert(results[3].as_string() == "Alice");
    assert(results[4].as_string() == "Order#100");
    assert(results[5].as_string() == "Widget");

    std::cout << "  -> Cluster pipeline dispatched across slots and reordered results successfully:\n";
    std::cout << "     user:1 -> " << results[3].as_string() << "\n";
    std::cout << "     order:100 -> " << results[4].as_string() << "\n";
    std::cout << "     item:500 -> " << results[5].as_string() << "\n";
}

core::Task<void> run_all_tests(core::IoUring& ring) {
    RedisNodeConfig standalone_cfg{
        .host = "127.0.0.1",
        .port = 6379,
        .password = "redis_secret"
    };
    RedisClient standalone(ring, standalone_cfg, 4);

    co_await test_lua_scripting(standalone);
    co_await test_distributed_locking(standalone);
    co_await test_sorted_set_ranges(standalone);
    co_await test_streams(standalone);
    co_await test_cluster_pipeline(ring);

    std::cout << "\n=======================================================\n";
    std::cout << " >>> ALL ADVANCED REDIS TESTS PASSED! <<<\n";
    std::cout << "=======================================================\n\n";
}

int main() {
    core::EventLoop loop(256);

    bool completed = false;
    loop.spawn([&]() -> core::Task<void> {
        try {
            co_await run_all_tests(loop.ring());
            completed = true;
        } catch (const std::exception& e) {
            std::cerr << "FATAL TEST FAILURE: " << e.what() << "\n";
            std::abort();
        }
        loop.stop();
    }());

    loop.run();
    assert(completed);
    return 0;
}
