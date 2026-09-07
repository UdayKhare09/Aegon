# Aegon C++26 Data Architecture & SQL ORM Plan

This document captures the design discussion, architectural decisions, and phased implementation roadmap for the **Aegon C++26 SQL Object-Relational Mapping (ORM)** system under the `aegon::data::orm::sql` namespace, designed as part of Aegon's modular data ecosystem.

---

## 0. Aegon Data Ecosystem Architecture

Aegon's data layer is modularized under `aegon::data` to separate shared types, relational SQL ORM, document databases, and caching layers:

```
aegon::data::
├── types/              # Foundational types (UUID, DateTime, Decimal, Json)
├── orm::
│   ├── sql::          # Relational SQL ORM (PostgreSQL, MySQL, SQLite)
│   └── mongo::        # Future: MongoDB / Document ODM
└── cache::
    ├── inmemory::     # Future: High-throughput thread-local / concurrent memory cache
    └── redis::        # Future: Async Redis client & distributed cache
```

This namespace separation ensures that the SQL ORM (`aegon::data::orm::sql`) does not conflict with document-based ODMs (`aegon::data::orm::mongo`) or caching subsystems (`aegon::data::cache::*`), while all subsystems share the unified zero-copy foundational types under `aegon::data::*`.

---

## 1. Foundational Data Types: JPA / Hibernate Comparison

In enterprise frameworks like Java's JPA/Hibernate, entity mapping relies on a rich set of database-compatible types beyond standard language primitives (`int`, `double`, `bool`, `std::string`).

### JPA vs. Aegon Data Types Matrix

| Category | JPA / SQL Type | Standard C++ | Aegon Status | Role in Aegon ORM |
| :--- | :--- | :--- | :--- | :--- |
| **Primary Keys** | `UUID` / `GUID` | None | **Implemented** (`aegon::data::UUID`) | Primary keys, v4 random, v7 time-ordered, SIMD accelerated, Glaze JSON registered. |
| **Temporal / Audit** | `TIMESTAMP`, `TIMESTAMPTZ` | `std::chrono::time_point` | **To Implement** (`aegon::data::DateTime`) | Crucial for `@CreationTimestamp` and `@UpdateTimestamp` audit fields. Zero-alloc ISO 8601 string formatting + SQL wire parsing. |
| **Date & Time** | `DATE`, `TIME` | `std::chrono::year_month_day` | **To Implement** (`aegon::data::Date`, `Time`) | Calendar dates and wall-clock time without timezone offsets. |
| **Fixed-Point / Money** | `NUMERIC(p, s)`, `DECIMAL` | `double` (causes precision loss) | **To Implement** (`aegon::data::Decimal<P, S>`) | Critical for financial/e-commerce data (`0.1 + 0.2 != 0.3` floating point bugs eliminated). |
| **Semi-Structured** | `JSON`, `JSONB` | `std::string` | **To Implement** (`aegon::data::Json`) | Hibernate `@Type(JsonType.class)` / Postgres `JSONB`. Embeds arbitrary JSON objects or nested DTOs into table columns. |
| **Raw Binary** | `BYTEA`, `BLOB` | `std::vector<uint8_t>` | **Built-in** (`std::vector<uint8_t>`) | File data, cryptographic hashes, raw byte buffers. |
| **Enums** | `@Enumerated(STRING/ORDINAL)` | `enum class` | **Glaze Built-in** | String or integer mapped enums via compile-time reflection. |

### Conclusion
Before building queries and migrations, Aegon needs foundational types (`DateTime`, `Decimal`, `Json`) so entities map directly to real-world SQL schemas without awkward workarounds.

---

## 2. Dynamic Database Dialect Mapping

SQL databases represent identical logical types with different native storage engines, column types, and placeholder syntaxes.

### Native Type Mapping Across Engines

| Logical C++ Type | PostgreSQL Native | MySQL / MariaDB Native | SQLite Native |
| :--- | :--- | :--- | :--- |
| **`UUID`** | `UUID` (16 bytes native) | `BINARY(16)` or `CHAR(36)` | `TEXT` or `BLOB` |
| **`DateTime`** | `TIMESTAMPTZ` | `DATETIME(6)` | `TEXT` (ISO-8601) / `INTEGER` |
| **`bool`** | `BOOLEAN` | `TINYINT(1)` | `INTEGER` (0 or 1) |
| **`Json`** | `JSONB` (binary indexed) | `JSON` | `TEXT` |
| **`std::vector<uint8_t>`** | `BYTEA` | `LONGBLOB` | `BLOB` |
| **`Decimal<18, 4>`** | `NUMERIC(18, 4)` | `DECIMAL(18, 4)` | `NUMERIC` |
| **Auto-Increment PK** | `BIGINT GENERATED ALWAYS AS IDENTITY` | `BIGINT AUTO_INCREMENT PRIMARY KEY` | `INTEGER PRIMARY KEY AUTOINCREMENT` |
| **Prepared Parameter** | `$1, $2, $3, ...` | `?, ?, ?, ...` | `?, ?` or `?1, ?2, ...` |

### Modern C++26 Dialect Architecture

#### Dialect Enum
```cpp
namespace aegon::data::orm::sql {

enum class DatabaseDialect : uint8_t {
    PostgreSQL,
    MySQL,
    SQLite
};

} // namespace aegon::data::orm::sql
```

#### TypeMapper Trait
Each type provides its native SQL definition and binding behavior for each dialect:

```cpp
namespace aegon::data::orm::sql {

template <typename T>
struct TypeMapper;

// Specialization for SIMD UUID:
template <>
struct TypeMapper<aegon::data::UUID> {
    static constexpr std::string_view column_type(DatabaseDialect dialect) noexcept {
        switch (dialect) {
            case DatabaseDialect::PostgreSQL: return "UUID";
            case DatabaseDialect::MySQL:      return "BINARY(16)";
            case DatabaseDialect::SQLite:     return "TEXT";
        }
        return "TEXT";
    }

    static void bind(const aegon::data::UUID& val, DatabaseDialect dialect, SqlBinder& binder) {
        if (dialect == DatabaseDialect::MySQL) {
            binder.bind_bytes(val.as_bytes(), 16);
        } else {
            char buf[36];
            val.to_chars(buf);
            binder.bind_text(std::string_view(buf, 36));
        }
    }
};

// Specialization for DateTime:
template <>
struct TypeMapper<aegon::data::DateTime> {
    static constexpr std::string_view column_type(DatabaseDialect dialect) noexcept {
        switch (dialect) {
            case DatabaseDialect::PostgreSQL: return "TIMESTAMPTZ";
            case DatabaseDialect::MySQL:      return "DATETIME(6)";
            case DatabaseDialect::SQLite:     return "TEXT";
        }
        return "TEXT";
    }
};

} // namespace aegon::data::orm::sql
```

---

## 3. Entity & Table Schema Definition (Zero-Macro, C++26 POCO)

### Design Principles
1. **Zero Macros**: No `#pragma db`, no `DECLARE_FIELD`, no code generators.
2. **Pure C++ Structs (POCO)**: Standard aggregate layout with zero memory overhead.
3. **Dual Use (Entity == DTO)**: Because entities are plain structs, they can be directly bound from and serialized to JSON using `ctx.bind_json<User>()` and `ctx.res().json(user)` without writing translation/mapping layers.
4. **Compile-time Pointer-to-Member Safety (`&User::field`)**: Refactoring a field in C++ immediately breaks obsolete queries at compile time.

### Entity Definition Example

```cpp
#include "data/orm/sql/Table.h"
#include "data/uuid/UUID.h"
#include "data/types/DateTime.h"
#include "data/types/Json.h"

struct User {
    // 1. Pure C++ fields
    aegon::data::UUID id;
    std::string email;
    std::string full_name;
    int age{0};
    std::optional<std::string> bio;  // std::optional automatically maps to NULLable column
    aegon::data::Json preferences;   // Maps to JSON / JSONB column
    aegon::data::DateTime created_at;
    aegon::data::DateTime updated_at;

    // 2. Compile-time Schema Metadata
    static constexpr auto schema() {
        return aegon::data::orm::sql::table<User>("users")
            .id(&User::id)                                           // Primary Key (UUID)
            .column(&User::email).unique().length(255)               // UNIQUE NOT NULL VARCHAR(255)
            .column(&User::full_name).column_name("name")            // Custom column name
            .column(&User::age).default_value(18)                    // DEFAULT 18
            .column(&User::bio)                                      // NULLable TEXT
            .column(&User::preferences)                              // JSONB / JSON
            .created_at(&User::created_at)                           // Auto UTC on INSERT
            .updated_at(&User::updated_at);                          // Auto UTC on UPDATE
    }
};
```

### Foreign Keys & Relationships

```cpp
struct Order {
    aegon::data::UUID id;
    aegon::data::UUID user_id;       // Foreign key scalar
    double total_amount{0.0};
    std::string status{"PENDING"};
    aegon::data::DateTime created_at;

    static constexpr auto schema() {
        return aegon::data::orm::sql::table<Order>("orders")
            .id(&Order::id)
            .column(&Order::user_id).references<User>(&User::id).on_delete_cascade()
            .column(&Order::total_amount)
            .column(&Order::status)
            .created_at(&Order::created_at);
    }
};
```

---

## 4. DDL Generation Output

Calling `aegon::data::orm::sql::generate_ddl<User>(dialect)` inspects the entity struct and asks the configured dialect for native column definitions:

### PostgreSQL:
```sql
CREATE TABLE IF NOT EXISTS users (
    id UUID PRIMARY KEY,
    email VARCHAR(255) NOT NULL UNIQUE,
    name TEXT NOT NULL,
    age INTEGER NOT NULL DEFAULT 18,
    bio TEXT,
    preferences JSONB NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);
```

### MySQL:
```sql
CREATE TABLE IF NOT EXISTS users (
    id BINARY(16) PRIMARY KEY,
    email VARCHAR(255) NOT NULL UNIQUE,
    name TEXT NOT NULL,
    age INT NOT NULL DEFAULT 18,
    bio TEXT NULL,
    preferences JSON NOT NULL,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6)
);
```

### SQLite:
```sql
CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY,
    email TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    age INTEGER NOT NULL DEFAULT 18,
    bio TEXT,
    preferences TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
```

---

## 5. Querying & Repository API (Spring Data / Active Record Style)

```cpp
// 1. Insert an entity
User user{
    .id = UUIDGenerator::v7(),
    .email = "uday@aegon.dev",
    .full_name = "Uday",
    .age = 28
};
co_await db.insert(user);

// 2. Type-safe find by primary key
std::optional<User> opt_user = co_await db.find_by_id<User>(user.id);

// 3. Type-safe Fluent Query Builder
std::vector<User> users = co_await db.from<User>()
    .where(&User::age, Op::Gte, 18)
    .and_where(&User::email, Op::Like, "%@aegon.dev")
    .order_by(&User::created_at, Order::Desc)
    .limit(20)
    .fetch_all();

// 4. Update with dirty tracking / partial column update
co_await db.from<User>()
    .where(&User::id, Op::Eq, user.id)
    .set(&User::age, 29)
    .update();

// 5. Delete
co_await db.delete_by_id<User>(user.id);
```

---

## 6. Implementation Roadmap

### Phase 1: Foundational Data Types
- [ ] `aegon::data::DateTime`: Microsecond precision, Unix epoch timestamp, zero-allocation ISO 8601 formatting/parsing, Glaze JSON specialization, SQL wire format support.
- [ ] `aegon::data::Decimal<P, S>`: 64-bit / 128-bit fixed-point arithmetic without floating point inaccuracy.
- [ ] `aegon::data::Json`: Type-safe wrapper for structured JSON column storage.

### Phase 2: Dialects & Schema Mapping Core
- [ ] `aegon::data::orm::sql::DatabaseDialect` enum (`PostgreSQL`, `MySQL`, `SQLite`).
- [ ] `aegon::data::orm::sql::TypeMapper<T>` traits for all primitives and custom types.
- [ ] `aegon::data::orm::sql::Table<Entity>` and `Column<Entity, T>` compile-time metadata builder.
- [ ] `aegon::data::orm::sql::generate_ddl<Entity>(dialect)` for automatic schema migration.

### Phase 3: Query Builder & Prepared Statements
- [ ] Dialect-aware SQL AST & parameter binder (`$1` for Postgres, `?` for MySQL/SQLite).
- [ ] Type-safe query builder (`from<T>()`, `.where()`, `.join()`, `.order_by()`, `.limit()`).

### Phase 4: Thread-per-Core Connection Pool & Async Coroutines
- [ ] Shared-nothing, lock-free connection pool integrated with `core::Task<T>`.
- [ ] Driver adapters (Postgres `libpq`/raw socket, MySQL, SQLite3).
