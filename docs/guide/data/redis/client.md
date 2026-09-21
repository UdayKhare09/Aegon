# Native Async Redis Client

Aegon includes a high-throughput, native Redis client engineered directly on top of Linux `io_uring` and C++26 coroutines. It bypasses external C libraries (like hiredis) to deliver zero-copy parsing of the RESP3 protocol and lock-free connection pooling.

---

## Client Instantiation

```cpp
#include <aegon/data/redis/RedisClient.h>

using namespace aegon::data::redis;

// Construct with event loop ring reference and node config
RedisNodeConfig config{
    .host = "127.0.0.1",
    .port = 6379,
    .password = "secret", // optional
    .database = 0
};

RedisClient redis(loop.ring(), config, /*pool_size=*/16);
```

---

## Strings API

```cpp
// Set key with optional TTL
co_await redis.set("user:session:100", "active", std::chrono::seconds(3600));

// Get value
std::optional<std::string> session = co_await redis.get("user:session:100");

// Atomic increment & decrement
int64_t counter = co_await redis.incr("metrics:page_views");
int64_t remaining = co_await redis.decr("inventory:item:42");

// Multi-key operations (MGET / MSET)
co_await redis.mset({{"k1", "v1"}, {"k2", "v2"}});
std::vector<std::optional<std::string>> values = co_await redis.mget({"k1", "k2", "k3"});

// Deletion
bool deleted = co_await redis.del("user:session:100");
```

---

## Hashes API

```cpp
// Set hash field
co_await redis.hset("user:42:profile", "username", "alex");
co_await redis.hset("user:42:profile", "email", "alex@example.com");

// Get single hash field
std::optional<std::string> email = co_await redis.hget("user:42:profile", "email");

// Check field existence
bool exists = co_await redis.hexists("user:42:profile", "phone");

// Fetch all field-value pairs
std::vector<std::pair<std::string, std::string>> all = co_await redis.hgetall("user:42:profile");
for (const auto& [field, val] : all) {
    std::cout << field << " = " << val << "\n";
}

// Delete field
co_await redis.hdel("user:42:profile", "bio");
```

---

## Lists & FIFO Queues

```cpp
// Push to list
co_await redis.lpush("job:queue", "task_1");
co_await redis.rpush("job:queue", "task_2");

// Pop from list
std::optional<std::string> task = co_await redis.lpop("job:queue");
std::optional<std::string> right = co_await redis.rpop("job:queue");
```

---

## Sets (Unordered Collections)

```cpp
// Add & remove set members
co_await redis.sadd("user:1:roles", "admin");
co_await redis.sadd("user:1:roles", "editor");
co_await redis.srem("user:1:roles", "editor");

// Membership test
bool is_admin = co_await redis.sismember("user:1:roles", "admin");

// Fetch all members
std::vector<std::string> roles = co_await redis.smembers("user:1:roles");
```

---

## Sorted Sets (Leaderboards & Priority Queues)

```cpp
// Add member with floating-point score
co_await redis.zadd("leaderboard", "player_1", 1500.0);
co_await redis.zadd("leaderboard", "player_2", 2100.5);

// Query rank and score
std::optional<int64_t> rank = co_await redis.zrevrank("leaderboard", "player_2"); // 0-indexed top rank
std::optional<double> score = co_await redis.zscore("leaderboard", "player_2");

// Range queries by rank
std::vector<std::pair<std::string, double>> top10 = 
    co_await redis.zrevrange_with_scores("leaderboard", 0, 9);

// Range queries by score
std::vector<std::string> contenders = 
    co_await redis.zrangebyscore("leaderboard", "1000", "2000");

// Cardinality
int64_t count = co_await redis.zcard("leaderboard");
```

---

## Key Lifecycle & Expiry

```cpp
// Set relative expiration
co_await redis.expire("temp_key", std::chrono::seconds(60));
co_await redis.pexpire("temp_key", std::chrono::milliseconds(500));

// Check remaining TTL
int64_t ttl_sec = co_await redis.ttl("temp_key"); // -1 if no TTL, -2 if not found

// Remove TTL (make persistent)
co_await redis.persist("temp_key");

// Check if key exists
bool found = co_await redis.exists("temp_key");
```

---

## Lua Scripting

Execute complex atomic server-side scripts via `EVAL` or cached `EVALSHA`:

```cpp
std::string lua = R"(
    local current = redis.call('GET', KEYS[1])
    if current == ARGV[1] then
        return redis.call('DEL', KEYS[1])
    else
        return 0
    end
)";

// Automatically computes SHA1, tries EVALSHA, and falls back to EVAL on NOSCRIPT
RespValue result = co_await redis.eval_script(lua, {"lock:order:42"}, {"token_abc"});
```

---

## RESP2 & RESP3 Data Model (`RespValue`)

Header file: `<aegon/data/redis/Resp3.h>`

When executing pipelines, transactions, Lua scripts, or raw commands via `redis.execute(...)`, responses are returned as `RespValue` instances. `RespValue` provides zero-allocation inspection and type conversions conforming to Redis RESP2 and RESP3 specifications:

| Method | Signature | Description |
|---|---|---|
| `is_null()` | `bool is_null() const noexcept` | True if nil representation (`$-1\r\n` or `_\r\n`). |
| `is_string()` | `bool is_string() const noexcept` | Checks for simple string (`+...`) or bulk string (`$...`). |
| `is_integer()` | `bool is_integer() const noexcept` | Checks if integer type (`:123\r\n`). |
| `is_error()` | `bool is_error() const noexcept` | Checks if error response (`-ERR ...`). |
| `is_array()` | `bool is_array() const noexcept` | Checks if array type (`*...`). |
| `as_string()` | `std::string as_string() const` | Returns string content. |
| `as_integer()` | `int64_t as_integer() const` | Returns 64-bit integer value. |
| `as_array()` | `const std::vector<RespValue>& as_array() const` | Returns nested array elements. |

```cpp
RespValue resp = co_await redis.execute({"HGETALL", "user:42"});

if (resp.is_array()) {
    const auto& fields = resp.as_array();
    for (size_t i = 0; i < fields.size(); i += 2) {
        std::string field = fields[i].as_string();
        std::string value = fields[i + 1].as_string();
        std::println("{}: {}", field, value);
    }
}
```

