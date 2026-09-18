# Database Configuration & Drivers

Aegon's SQL subsystem provides asynchronous, non-blocking drivers for **PostgreSQL** and **SQLite3**, backed by a thread-per-core connection pool (`PerCoreConnectionPool`) and automated read-replica load balancing.

---

## Configuration via `SqlConfig`

Configure database connections using `SqlConfig` with designated initializers:

```cpp
#include <aegon/data/orm/sql/SqlConfig.h>
#include <aegon/data/orm/sql/SqlDatabaseClient.h>

using namespace aegon::data::orm::sql;

// 1. PostgreSQL Configuration
SqlConfig pg_config{
    .dialect = DatabaseDialect::PostgreSQL,
    .host = "127.0.0.1",
    .port = 5432,
    .database = "production_db",
    .user = "aegon_user",
    .password = "secret_password",
    .pool_per_core = 4 // 4 connections per worker thread / CPU core
};

// 2. SQLite Configuration
SqlConfig sqlite_config{
    .dialect = DatabaseDialect::SQLite,
    .database = "/var/data/app.db", // or ":memory:" for in-memory DB
    .pool_per_core = 2
};
```

---

## Supported Database Engines

### 1. PostgreSQL (`drivers::PostgresDriver`)
- Connects asynchronously via `libpq`.
- Supports binary and text parameter binding.
- Handles PostgreSQL-specific SQL syntax, placeholders (`$1, $2, ...`), and `RETURNING` clauses for auto-incrementing primary keys.

### 2. SQLite (`drivers::SqliteDriver`)
- Embeds high-performance `sqlite3` engine.
- Automatically enables Write-Ahead Logging (`WAL` mode) and foreign key enforcement (`PRAGMA foreign_keys = ON;`).
- Thread-safe connection isolation per core (`SQLITE_OPEN_NOMUTEX`).

---

## Per-Core Connection Pooling (`PerCoreConnectionPool`)

In high-concurrency multi-threaded servers, sharing a single global connection pool across threads creates severe lock contention.

Aegon uses **`PerCoreConnectionPool`**:

```
[ Worker Core 0 ] ──► Leases from Core 0 Pool (Zero lock contention)
[ Worker Core 1 ] ──► Leases from Core 1 Pool (Zero lock contention)
[ Worker Core 2 ] ──► Leases from Core 2 Pool (Zero lock contention)
```

Each worker core leases from its own pre-allocated slice of database connections, completely eliminating cross-thread mutex acquisition on queries.

### Setting up the Client

```cpp
// 1. Create the primary per-core connection pool
PerCoreConnectionPool primary_pool(pg_config);

// 2. Initialize the SqlDatabaseClient
auto db = std::make_shared<SqlDatabaseClient>(primary_pool);

// 3. Register in the Service Registry
server.provide<SqlDatabaseClient>(db);
```

---

## Read-Replica Load Balancing

For high-read applications, Aegon supports splitting read traffic across secondary replica pools while directing writes and transactions strictly to the primary:

```cpp
// Primary write pool
PerCoreConnectionPool primary_pool(primary_config);

// Secondary read-replica pools
PerCoreConnectionPool replica_1_pool(replica_1_config);
PerCoreConnectionPool replica_2_pool(replica_2_config);

std::vector<std::reference_wrapper<PerCoreConnectionPool>> replicas = {
    replica_1_pool, 
    replica_2_pool
};

// Client automatically routes:
// • SELECT queries -> Round-robin across replicas
// • INSERT/UPDATE/DELETE/Transactions -> Primary write pool
auto db = std::make_shared<SqlDatabaseClient>(primary_pool, replicas);
```

---

## Automated DDL Migrations (`sync_schema`)

Aegon can inspect your compile-time C++ `TableDef` schemas and automatically generate and execute the corresponding `CREATE TABLE` and `CREATE INDEX` statements:

```cpp
server.on_start([](Server& s) -> Task<void> {
    auto db = s.service<SqlDatabaseClient>();

    // Generates and runs DDL for User, Order, and Product
    std::cout << "Synchronizing database schema...\n";
    co_await db->sync_schema<User, Order, Product>();
    std::cout << "Schema synchronization complete.\n";

    co_return;
});
```
