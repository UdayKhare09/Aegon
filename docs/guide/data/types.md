# Foundational Data Types

Aegon provides a suite of zero-overhead, memory-efficient foundational data types defined in `<aegon/data/types/Types.h>`.

These types are framework-wide primitives: they are supported out of the box by **HTTP JSON DTO serialization/validation**, **SQL ORM table mappings**, and **Redis protocol storage**.

---

## Overview of Types

```cpp
#include <aegon/data/types/Types.h>

using namespace aegon::data::types;
```

| Type | Underlying Size | Purpose | Key Capabilities |
| :--- | :--- | :--- | :--- |
| **`UUID`** | 16 bytes | Unique Identifiers | AVX-512 / SIMD-accelerated v4 generation & parsing, nil checks. |
| **`DateTime`** | 8 bytes (64-bit) | Timestamps | Microsecond/nanosecond UTC timestamp, ISO 8601 formatting (`2026-09-19T00:00:00Z`). |
| **`Date`** | 4 bytes (32-bit) | Calendar Date | Fast Howard Hinnant civil calendar algorithms (`YYYY-MM-DD`). |
| **`Time`** | 8 bytes (64-bit) | Time of Day | Microsecond precision time of day (`HH:MM:SS.ffffff`). |
| **`Decimal`** | 16 bytes (128-bit) | Financial Math | Exact fixed-point scaled integer, zero IEEE-754 binary floating-point roundoff bugs. |
| **`IpAddress`**| 16 bytes | Networking | Dual IPv4 and IPv6 address parsing, CIDR subnet matching (`in_subnet`). |
| **`MacAddress`**| 6 bytes | Hardware ID | 48-bit IEEE 802 MAC address parsing and formatting (`aa:bb:cc:dd:ee:ff`). |
| **`Json`** | Variable | Structured JSON | Dynamic JSON wrapper integrating with Glaze serialization. |
| **`Blob`** | Variable | Raw Binary | Contiguous byte storage (`std::vector<uint8_t>`) with SQL hex encoding (`\x...`). |
| **`Hash256`** | 32 bytes | Cryptography | Fixed 256-bit SHA-256 digest container with hex conversion. |

---

## 1. `UUID` & `UUIDGenerator` (RFC 9562)

Aegon's `UUID` uses SIMD instructions (AVX-512, SSE4.2, or ARM NEON) for parsing and hardware entropy generation:

```cpp
// 1. Generate random UUIDv4
UUID id_v4 = UUIDGenerator::v4();

// 2. Generate time-ordered sortable UUIDv7
UUID id_v7 = UUIDGenerator::v7();
uint64_t ts_ms = id_v7.timestamp_ms(); // 48-bit UNIX millisecond timestamp

// 3. Vectorized batch generation (high-throughput ingest)
std::vector<UUID> batch(1000);
UUIDGenerator::v7_batch(batch);

// 4. Zero-allocation SIMD format into 36-byte stack buffer
char buf[37];
id_v7.to_chars(buf);
buf[36] = '\0';

// 5. Fast SIMD parsing
auto parsed = UUID::from_string("550e8400-e29b-41d4-a716-446655440000");
if (parsed && !parsed->is_nil()) {
    std::string str = parsed->to_string();
}

// 6. 16-byte raw buffer view
std::span<const uint8_t, 16> raw_bytes = id_v7.as_bytes();
```

| Method | Signature | Description |
|---|---|---|
| `UUIDGenerator::v4()` | `static UUID v4() noexcept` | Cryptographically secure random UUIDv4. |
| `UUIDGenerator::v7()` | `static UUID v7() noexcept` | Time-ordered sortable UUIDv7 with sub-millisecond counter. |
| `UUIDGenerator::v4_batch()` | `static void v4_batch(std::span<UUID> out) noexcept` | Vectorized batch generation of UUIDv4s. |
| `UUIDGenerator::v7_batch()` | `static void v7_batch(std::span<UUID> out) noexcept` | Vectorized batch generation of monotonically increasing UUIDv7s. |
| `to_chars()` | `char* to_chars(char* out) const noexcept` | Zero-allocation SIMD format into 36-byte output buffer. |
| `timestamp_ms()` | `constexpr uint64_t timestamp_ms() const noexcept` | Extracts 48-bit UNIX millisecond timestamp from UUIDv7. |
| `is_nil()` | `constexpr bool is_nil() const noexcept` | Checks if UUID is all zeroes. |

---

## 2. `DateTime`, `Date`, and `Time`

High-speed calendar and timestamp types optimized for database wire formats:

```cpp
// Current UTC timestamp
DateTime now = DateTime::now();
std::string iso = now.to_iso8601(); // "2026-09-19T00:25:30.123456Z"

// Parse ISO-8601 string
auto dt = DateTime::from_iso8601("2026-09-19T14:30:00Z");

// Civil Date representation
Date today(2026, 9, 19);
Date tomorrow = today.add_days(1);
std::string date_str = today.to_string(); // "2026-09-19"

// Time of Day
Time meeting(14, 30, 0); // 14:30:00
std::string time_str = meeting.to_string();
```

---

## 3. `Decimal` (Fixed-Point Financial Math)

Binary floating-point types (`float`, `double`) cannot accurately represent decimals like `0.1 + 0.2 != 0.3`. Aegon's `Decimal<Precision, Scale>` uses exact 128-bit integer scaling matching SQL `DECIMAL(18, 4)`:

```cpp
// Standard financial decimal: Decimal<18, 4>
Decimal balance("1250.5000");
Decimal cost("49.9900");

// Arithmetic without floating-point error
Decimal remainder = balance - cost;

// Comparison
if (remainder.is_positive()) {
    std::string formatted = remainder.to_string(); // "1200.5100"
}

// Convert to double when displaying estimates
double approx = remainder.to_double();
```

---

## 4. `IpAddress` (Dual IPv4 & IPv6)

A single 16-byte structure holding either IPv4 or IPv6 network addresses:

```cpp
// Construct IPv4
IpAddress client_ip(192, 168, 1, 50);
auto parsed_v4 = IpAddress::from_string("10.0.0.1");

// Construct IPv6
auto parsed_v6 = IpAddress::from_string("2001:db8::1");

// CIDR Subnet Membership
IpAddress subnet = *IpAddress::from_string("192.168.1.0");
bool matches = client_ip.in_subnet(subnet, 24); // true (192.168.1.50 is inside /24)

// Introspection
bool is_priv = client_ip.is_private();   // true
bool is_loop = client_ip.is_loopback();  // false
```

---

## 5. `Blob`, `Hash256`, and `MacAddress`

```cpp
// Binary payload with hex & Base64 conversions
Blob payload;
payload.assign({0xDE, 0xAD, 0xBE, 0xEF});
std::string hex_str = payload.to_hex();       // "deadbeef"
std::string b64_str = payload.to_base64();    // "3q2+7w=="
auto decoded = Blob::from_base64(b64_str);

// Cryptographic hash container with constant-time equality check (prevents timing attacks)
Hash256 sha1 = Hash256::from_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855").value();
Hash256 sha2 = Hash256::from_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855").value();
bool match = (sha1 == sha2); // Constant-time comparison

// Hardware MAC Address
MacAddress mac = *MacAddress::from_string("00:1A:2B:3C:4D:5E");
std::string formatted_mac = mac.to_string();
```

---

## Cross-Framework Integration

### In HTTP DTOs (Automatic JSON & Validation)

All foundational types serialize directly via Glaze without writing boilerplate serializers:

```cpp
struct UserProfileDto {
    UUID user_id;
    DateTime created_at;
    Decimal<18, 2> account_balance;
    IpAddress last_login_ip;

    void validate(aegon::validation::ValidationRules& v) const {
        v.field("user_id", user_id).not_nil();
        v.field("account_balance", account_balance).positive();
    }
};
```

### In SQL ORM Schemas

```cpp
struct Account {
    UUID id;
    Decimal<18, 4> balance;
    DateTime created_at;
    DateTime updated_at;

    static auto schema() {
        return TableDef<Account>("accounts")
            .id(&Account::id)
            .column(&Account::balance).not_null()
            .created_at(&Account::created_at)
            .updated_at(&Account::updated_at);
    }
};
```
