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
| **Temporal / Audit** | `TIMESTAMP`, `TIMESTAMPTZ` | `std::chrono::time_point` | **Implemented** (`aegon::data::DateTime`) | Crucial for `@CreationTimestamp` and `@UpdateTimestamp` audit fields. Zero-alloc ISO 8601 string formatting + SQL wire parsing. |
| **Date & Time** | `DATE`, `TIME` | `std::chrono::year_month_day` | **Implemented** (`aegon::data::Date`, `Time`) | Calendar dates and wall-clock time without timezone offsets. |
| **Fixed-Point / Money** | `NUMERIC(p, s)`, `DECIMAL` | `double` (causes precision loss) | **Implemented** (`aegon::data::Decimal<P, S>`) | Critical for financial/e-commerce data (`0.1 + 0.2 == 0.3` floating point bugs eliminated). |
| **Semi-Structured** | `JSON`, `JSONB` | `std::string` | **Implemented** (`aegon::data::Json`) | Hibernate `@Type(JsonType.class)` / Postgres `JSONB`. Embeds arbitrary JSON objects or nested DTOs into table columns. |
| **Network** | `INET`, `MACADDR` | None | **Implemented** (`aegon::data::IpAddress`, `MacAddress`) | Dual IPv4/IPv6 address with CIDR subnet matching + 48-bit MAC address. |
| **Binary & Crypto** | `BYTEA`, `BLOB`, `CHAR(64)` | `std::vector<uint8_t>` | **Implemented** (`aegon::data::Blob`, `Hash256`) | Binary data with Base64 JSON support + 32-byte hash with constant-time equality check. |
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

### Phase 1: Foundational Data Types (COMPLETED - 100% Header-Only)
- [x] `aegon::data::types::UUID`: 128-bit RFC 9562 v4 & v7 SIMD accelerated, Glaze JSON registered.
- [x] `aegon::data::types::DateTime`: Microsecond precision UTC timestamp, zero-allocation ISO 8601 formatting/parsing, Glaze JSON, timezone offsets (`+05:30`).
- [x] `aegon::data::types::Date`: Gregorian calendar date (`YYYY-MM-DD`), leap year check, comparison, Glaze JSON.
- [x] `aegon::data::types::Time`: 24-hour wall-clock time (`HH:MM:SS.ffffff`), microsecond resolution, Glaze JSON.
- [x] `aegon::data::types::Decimal<P, S>`: 128-bit fixed-point arithmetic without binary float errors (`0.10 + 0.20 == 0.30`).
- [x] `aegon::data::types::Json`: Validated JSON column wrapper with lazy `.get<T>()` and raw unquoted JSON embedding.
- [x] `aegon::data::types::IpAddress`: Dual IPv4 & IPv6 with CIDR subnet matching (`in_subnet`).
- [x] `aegon::data::types::MacAddress`: 48-bit IEEE 802 hardware address.
- [x] `aegon::data::types::Blob`: Binary buffer with Base64 JSON conversion.
- [x] `aegon::data::types::Hash256`: 32-byte cryptographic token with constant-time equality comparisons.
- [x] `aegon::data::types::Types.h`: Umbrella header exporting all types.
- [x] `aegon::validation::Validator`: Enhanced with `.positive()`, `.past()`, `.future()`, `.not_nil()`.
- [x] `test_foundational_types.cpp`: Comprehensive 11-test suite passing with 0 warnings.

### Phase 2: Dialects & Schema Mapping Core (`aegon::data::orm::sql`) (COMPLETED - 100% Header-Only)
- [x] `aegon::data::orm::sql::DatabaseDialect` enum (`PostgreSQL`, `MySQL`, `SQLite`).
- [x] `aegon::data::orm::sql::DialectTraits`: Quoting, parameter placeholders, auto-increment PK idioms, and timestamp expressions.
- [x] `aegon::data::orm::sql::TypeMapper<T>` traits specializing primitives, `std::optional<T>`, `std::vector<uint8_t>`, and all foundational data types (`UUID`, `DateTime`, `Date`, `Time`, `Decimal<P, S>`, `Json`, `IpAddress`, `MacAddress`, `Blob`, `Hash256`).
- [x] `aegon::data::orm::sql::TableDef<Entity>` fluent builder with compile-time member pointers (`&Entity::field`), primary keys, auto-increment detection, lengths, uniqueness, nullability, foreign keys (`references<TargetEntity>()`), cascade rules, and audit timestamps.
- [x] `aegon::data::orm::sql::generate_ddl<Entity>(dialect)` producing production-grade, dialect-native DDL strings with foreign keys, constraints, and audit timestamp triggers/defaults.
- [x] `aegon::data::orm::sql::generate_indexes<Entity>(dialect)` and `generate_drop_table<Entity>(dialect)`.
- [x] `tests/test_sql_schema.cpp`: 6 comprehensive multi-dialect DDL and type mapping unit tests passing with 0 warnings (verified compatible with SQLite 3 and PostgreSQL 17).


### Phase 3: Compile-Time Type-Safe SQL Query Builder & Auto-Mapping (COMPLETED - 100% Header-Only)
- [x] Expression Tree & Operator Model (`Op::Eq`, `Op::Neq`, `Op::Gt`, `Op::Gte`, `Op::Lt`, `Op::Lte`, `Op::Like`, `Op::In`, `Op::Between`, `Op::IsNull`, `Op::IsNotNull`).
- [x] Type-safe field resolution via compile-time member pointer mapping (`&Entity::field` -> SQL column name via offset tracking).
- [x] `RowView` database-agnostic row abstraction with `MockRowView` and typed conversion for primitives, optionals, and foundational types.
- [x] **Bi-Directional Auto-Mapping**:
  - `Row` $\rightarrow$ `Entity` Hydration: `from<Entity>().map_row(row)` and `.map_rows(rows)` directly populating pure C++ POCO structs.
  - `Entity` $\rightarrow$ SQL Parameters Extraction: `insert_into<Entity>().values(entity)` and `update<Entity>().set_entity(entity)` automatically decomposing entity fields into SQL parameters.
- [x] `SelectBuilder<Entity>`:
  - Custom column projection (`.select(&Entity::col1, &Entity::col2)`) and full entity projection (`from<Entity>()`).
  - Fluent `.where()`, `.and_where()`, `.or_where()`, `.where_in()`, `.where_between()`, `.where_null()`, `.where_not_null()`.
  - `.order_by(&Entity::field, SortOrder::Asc | SortOrder::Desc)`.
  - `.limit(n)`, `.offset(n)`.
  - Multi-dialect placeholder generation (`$1, $2` for PostgreSQL, `?, ?` for MySQL and SQLite).
- [x] `InsertBuilder<Entity>`:
  - Single and batch entity inserts with automatic extraction.
  - PostgreSQL auto-increment identity handling (`RETURNING id`).
- [x] `UpdateBuilder<Entity>`:
  - Partial column updates (`.set(&Entity::field, val)`) and full entity updates (`.set_entity(entity)`).
- [x] `DeleteBuilder<Entity>`:
  - Conditional deletion queries (`delete_from<Entity>().where(...)`).
- [x] `tests/test_sql_query_builder.cpp`: Comprehensive 5-test suite passing with zero warnings on GCC 16.2.1 `-std=c++26 -O3 -Wall -Wextra -Wpedantic`.

### Phase 4: Thread-per-Core Connection Pool & Option 4 Transactions (COMPLETED - 100% Header-Only)
- [x] Multi-database namespace on `Context`: `ctx.db.sql` directly available in all route handlers, leaving clean namespaces for `ctx.db.mongo` and `ctx.cache.{redis, inmemory}`.
- [x] Shared-nothing, lock-free per-core connection pool (`PerCoreConnectionPool`) with zero mutex contention and RAII `ConnectionGuard`.
- [x] **Option 4: Transaction & Unit of Work**:
  - `co_await ctx.db.sql.transaction([&](Transaction& tx) -> Task<void> { ... })` with atomic multi-table execution.
  - Automatic `COMMIT` on normal block completion.
  - Automatic `ROLLBACK` on exception or failure with proper error propagation.
- [x] Transactional and direct single-operation entity CRUD: `insert(entity)`, `update_entity(entity)`, `find_by_id<T>(id)`, `delete_by_id<T>(id)`, `fetch_all(builder)`, `fetch_one(builder)`.
- [x] `MockConnection` driver for deterministic asynchronous unit testing.
- [x] `tests/test_sql_transaction.cpp`: 5 unit tests validating transaction commits, rollbacks, `ctx.db.sql`, direct CRUD, and lock-free pool reuse with zero warnings.



