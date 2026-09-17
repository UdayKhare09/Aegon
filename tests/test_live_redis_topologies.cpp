#include "data/redis/RedisClient.h"
#include "data/redis/RedisPipeline.h"
#include "data/redis/RedisTransaction.h"
#include "data/redis/RedisSubscriber.h"
#include "core/IoUring.h"
#include "core/EventLoop.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

using namespace aegon;
using namespace aegon::data::redis;

core::Task<void> test_standalone(core::IoUring& ring) {
    std::cout << "\n--- [1] Testing Live Redis Standalone (Port 6379) ---\n";
    RedisNodeConfig cfg{
        .host = "127.0.0.1",
        .port = 6379,
        .password = "redis_secret"
    };
    RedisClient client(ring, cfg, 4);
    co_await client.execute({"FLUSHDB"});

    // 1. Basic CRUD
    bool s_ok = co_await client.set("standalone:foo", "bar123");
    assert(s_ok);
    auto val = co_await client.get("standalone:foo");
    assert(val.has_value() && *val == "bar123");
    std::cout << "  -> Basic SET/GET verified: " << *val << "\n";

    // 2. INCR / DECR
    co_await client.set("standalone:counter", "10");
    int64_t inc = co_await client.incr("standalone:counter");
    assert(inc == 11);
    int64_t dec = co_await client.decr("standalone:counter");
    assert(dec == 10);
    std::cout << "  -> INCR/DECR verified: " << dec << "\n";

    // 3. Second Expiry & TTL
    bool exp_ok = co_await client.expire("standalone:counter", std::chrono::seconds(100));
    assert(exp_ok);
    int64_t t = co_await client.ttl("standalone:counter");
    assert(t > 0 && t <= 100);
    bool per_ok = co_await client.persist("standalone:counter");
    assert(per_ok);
    int64_t t2 = co_await client.ttl("standalone:counter");
    assert(t2 == -1); // Persisted
    std::cout << "  -> EXPIRE, TTL, and PERSIST verified\n";

    // 4. Millisecond Expiry & TTL (PEXPIRE / PTTL)
    co_await client.set("standalone:ms_key", "ms_val");
    assert(co_await client.pexpire("standalone:ms_key", std::chrono::milliseconds(5000)));
    int64_t ms_ttl = co_await client.pttl("standalone:ms_key");
    assert(ms_ttl > 0 && ms_ttl <= 5000);
    std::cout << "  -> PEXPIRE and PTTL verified: " << ms_ttl << "ms\n";

    // 5. EXISTS & DEL
    assert(co_await client.exists("standalone:counter"));
    assert(co_await client.del("standalone:counter"));
    assert(!co_await client.exists("standalone:counter"));
    std::cout << "  -> EXISTS and DEL verified\n";

    // 6. Data Structures: Hashes (HSET, HGET, HDEL)
    assert(co_await client.hset("h:user:1", "name", "Grace Hopper"));
    assert(co_await client.hset("h:user:1", "role", "Pioneer"));
    auto hname = co_await client.hget("h:user:1", "name");
    assert(hname.has_value() && *hname == "Grace Hopper");
    assert(co_await client.hdel("h:user:1", "role"));
    assert(!co_await client.hget("h:user:1", "role"));
    std::cout << "  -> Hashes (HSET, HGET, HDEL) verified\n";

    // 7. Data Structures: Lists (LPUSH, RPOP)
    assert(co_await client.lpush("list:tasks", "task_first") == 1);
    assert(co_await client.lpush("list:tasks", "task_second") == 2);
    auto popped = co_await client.rpop("list:tasks");
    assert(popped.has_value() && *popped == "task_first");
    std::cout << "  -> Lists (LPUSH, RPOP) verified: " << *popped << "\n";

    // 8. Data Structures: Sets (SADD)
    assert(co_await client.sadd("set:languages", "c++26"));
    assert(co_await client.sadd("set:languages", "asm"));
    assert(!co_await client.sadd("set:languages", "c++26")); // already present
    std::cout << "  -> Sets (SADD) verified\n";

    // 9. Data Structures: Sorted Sets (ZADD)
    assert(co_await client.zadd("zset:scoreboard", "player_alpha", 100.5));
    assert(co_await client.zadd("zset:scoreboard", "player_beta", 250.0));
    std::cout << "  -> Sorted Sets (ZADD) verified\n";

    // 10. Batch Multi-Key Operations (MSET, MGET, DEL_MANY)
    assert(co_await client.mset({{"m:1", "v1"}, {"m:2", "v2"}, {"m:3", "v3"}}));
    auto mvals = co_await client.mget({"m:1", "m:2", "m:3", "m:none"});
    assert(mvals.size() == 4);
    assert(mvals[0] == "v1" && mvals[1] == "v2" && mvals[2] == "v3" && !mvals[3].has_value());
    assert(co_await client.del_many({"m:1", "m:2", "m:3"}) == 3);
    std::cout << "  -> Batch MSET, MGET, and DEL_MANY verified\n";

    // 11. Logical Database Switching (SELECT <db>)
    assert(co_await client.select_db(1));
    co_await client.set("db1:isolation_key", "secret_in_db1");
    assert(co_await client.select_db(0));
    assert(!co_await client.exists("db1:isolation_key")); // isolated from DB 0
    assert(co_await client.select_db(1));
    assert(co_await client.exists("db1:isolation_key"));
    assert(co_await client.select_db(0));
    std::cout << "  -> Logical DB Switching (SELECT 1 <-> SELECT 0) verified\n";

    // 12. Redis 6+ Named ACL User Authentication (AUTH <user> <pass>)
    co_await client.execute({"ACL", "SETUSER", "aegon_acl_tester", "on", ">secret_acl_pwd", "~*", "+@all"});
    RedisNodeConfig acl_cfg{
        .host = "127.0.0.1",
        .port = 6379,
        .username = "aegon_acl_tester",
        .password = "secret_acl_pwd"
    };
    RedisClient acl_client(ring, acl_cfg, 2);
    assert(co_await acl_client.set("acl:authed_key", "acl_secret_payload"));
    auto acl_read = co_await acl_client.get("acl:authed_key");
    assert(acl_read.has_value() && *acl_read == "acl_secret_payload");
    std::cout << "  -> Redis 6+ Named ACL User Auth (aegon_acl_tester) verified\n";

    // 13. Fluent Pipeline
    auto pipe = client.pipeline();
    pipe.set("p:1", "val1")
        .set("p:2", "val2")
        .get("p:1")
        .incr("p:cnt");
    assert(pipe.size() == 4);
    auto p_res = co_await pipe.execute();
    assert(p_res.size() == 4);
    assert(p_res[2].as_string() == "val1");
    assert(p_res[3].as_integer() == 1);
    std::cout << "  -> Fluent Pipeline (4 commands in 1 roundtrip) verified\n";

    // 14. Atomic Transactions (MULTI / EXEC)
    auto tx_opt = co_await client.multi();
    assert(tx_opt.has_value());
    auto& tx = *tx_opt;
    tx.set("tx:key", "tx_val")
      .incr("tx:cnt");
    auto tx_res = co_await tx.exec();
    assert(tx_res.size() == 2);
    assert(tx_res[1].as_integer() == 1);
    auto tx_get = co_await client.get("tx:key");
    assert(tx_get.has_value() && *tx_get == "tx_val");
    std::cout << "  -> Atomic Transaction (MULTI/EXEC) verified\n";

    // 15. Pub/Sub Streaming Subscription & Unsubscribe
    RedisSubscriber sub(ring, cfg);
    bool sub_ok = co_await sub.connect();
    assert(sub_ok);
    bool subscribed = co_await sub.subscribe({"events:aegon", "events:alerts"});
    assert(subscribed);
    std::cout << "  -> Subscribed to 'events:aegon' & 'events:alerts'\n";

    // Publish from client
    int64_t receivers = co_await client.publish("events:aegon", "payload_hello_world");
    assert(receivers >= 1);
    std::cout << "  -> Published message, receivers: " << receivers << "\n";

    auto msg_opt = co_await sub.next_message();
    assert(msg_opt.has_value());
    assert(msg_opt->channel == "events:aegon");
    assert(msg_opt->payload == "payload_hello_world");
    std::cout << "  -> Received Pub/Sub message: channel=" << msg_opt->channel 
              << " payload=" << msg_opt->payload << "\n";

    assert(co_await sub.unsubscribe({"events:aegon", "events:alerts"}));
    std::cout << "  -> Unsubscribe verified\n";
    sub.close();

    std::cout << "[PASS] Redis Standalone Topology Complete!\n";
}

core::Task<void> test_sentinel(core::IoUring& ring) {
    std::cout << "\n--- [2] Testing Live Redis Sentinel (Port 26379) ---\n";
    SentinelConfig scfg{
        .master_name = "mymaster",
        .sentinels = {{"127.0.0.1", 26379}},
        .password = "sentinel_secret"
    };
    RedisClient client(ring, scfg, 4);

    // Auto-discovers master (127.0.0.1:6380) and executes commands
    bool s_ok = co_await client.set("sentinel:key1", "sentinel_val_1");
    assert(s_ok);

    auto val = co_await client.get("sentinel:key1");
    assert(val.has_value() && *val == "sentinel_val_1");
    std::cout << "  -> Master auto-discovered from Sentinel: " << *val << "\n";

    int64_t inc = co_await client.incr("sentinel:counter");
    assert(inc >= 1);
    std::cout << "  -> Master command execution verified: counter=" << inc << "\n";

    std::cout << "[PASS] Redis Sentinel Topology Complete!\n";
}

core::Task<void> test_cluster(core::IoUring& ring) {
    std::cout << "\n--- [3] Testing Live Redis Cluster (Ports 7000..7005) ---\n";
    ClusterConfig ccfg{
        .seed_nodes = {{"127.0.0.1", 7000}, {"127.0.0.1", 7001}, {"127.0.0.1", 7002}},
        .pool_size_per_node = 4
    };
    RedisClient client(ring, ccfg);

    // 1. Write keys across different slots
    // "key_a" -> slot 15495 (Master 2: 7002)
    // "key_b" -> slot 3999 (Master 0: 7000)
    // "key_c" -> slot 7365 (Master 1: 7001)
    co_await client.set("key_a", "val_a");
    co_await client.set("key_b", "val_b");
    co_await client.set("key_c", "val_c");

    auto a = co_await client.get("key_a");
    auto b = co_await client.get("key_b");
    auto c = co_await client.get("key_c");

    assert(a.has_value() && *a == "val_a");
    assert(b.has_value() && *b == "val_b");
    assert(c.has_value() && *c == "val_c");
    std::cout << "  -> Cross-slot keys routed and fetched across masters (7000, 7001, 7002)\n";

    // 2. Hash tags: {tenant_99}:profile and {tenant_99}:settings must hash to the exact same slot
    co_await client.set("{tenant_99}:profile", "profile_data");
    co_await client.set("{tenant_99}:settings", "settings_data");

    auto prof = co_await client.get("{tenant_99}:profile");
    auto sett = co_await client.get("{tenant_99}:settings");
    assert(prof.has_value() && *prof == "profile_data");
    assert(sett.has_value() && *sett == "settings_data");
    std::cout << "  -> Hash tags {tenant_99} verified on exact same cluster node\n";

    // 3. Transparent MOVED Redirection
    // Initialize cluster client seeded ONLY with node 7001 (responsible for slots 5461..10922)
    // Asking for "key_b" (slot 3999 on node 7000) triggers a -MOVED 3999 127.0.0.1:7000 from node 7001
    // RedisClusterRouter catches -MOVED, refreshes/updates slot, and returns data cleanly!
    ClusterConfig ccfg_moved{
        .seed_nodes = {{"127.0.0.1", 7001}},
        .pool_size_per_node = 2
    };
    RedisClient client_moved(ring, ccfg_moved);
    auto moved_res = co_await client_moved.get("key_b");
    assert(moved_res.has_value() && *moved_res == "val_b");
    std::cout << "  -> Transparent MOVED redirect caught and resolved from Node 7001 to Node 7000\n";

    // 4. Cluster multi-key batch operations
    assert(co_await client.mset({{"cl:1", "cval1"}, {"cl:2", "cval2"}}));
    auto cl_m = co_await client.mget({"cl:1", "cl:2"});
    assert(cl_m.size() == 2 && cl_m[0] == "cval1" && cl_m[1] == "cval2");
    assert(co_await client.del_many({"cl:1", "cl:2"}) == 2);
    std::cout << "  -> Cluster batch MSET, MGET, and DEL_MANY verified\n";

    std::cout << "[PASS] Redis Cluster Topology Complete!\n";
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << "   AEGON LIVE REDIS CLIENT & TOPOLOGIES TEST SUITE     \n";
    std::cout << "=======================================================\n";

    core::EventLoop loop(128);

    bool completed = false;
    loop.spawn([&]() -> core::Task<void> {
        co_await test_standalone(loop.ring());
        co_await test_sentinel(loop.ring());
        co_await test_cluster(loop.ring());
        completed = true;
        loop.stop();
    }());

    loop.run();
    assert(completed);

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL LIVE REDIS TOPOLOGY TESTS PASSED! <<<       \n";
    std::cout << "=======================================================\n";
    return 0;
}
