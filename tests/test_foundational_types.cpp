#include "data/types/Types.h"
#include "data/validation/Validator.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <optional>

#if __has_include(<glaze/glaze.hpp>)
#include <glaze/glaze.hpp>
#endif

#define TEST_CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace aegon::data;
using namespace aegon::validation;

// File-scope DTOs for Glaze reflection
struct InnerDTO {
    std::string role;
    int level{0};
};

struct ParentDTO {
    std::string title;
    Json config;
};

// 11. Full Aggregate DTO containing all foundational types
struct EnterpriseRecordDTO {
    UUID id;
    std::string title;
    DateTime created_at;
    Date effective_date;
    Time shift_start;
    Decimal128 balance;
    Json metadata;
    IpAddress server_ip;
    MacAddress hardware_mac;
    Blob payload;
    Hash256 sha256_checksum;

    void validate(ValidationRules& v) const {
        v.field("id", id).not_nil();
        v.field("title", title).required();
        v.field("balance", balance).positive();
    }
};

void test_uuid() {
    std::cout << "[Test 1] Testing UUID (v4, v7, SIMD parsing, Glaze)...\n";
    UUID id1 = UUIDGenerator::v4();
    TEST_CHECK(id1.version() == 4);
    TEST_CHECK(!id1.is_nil());

    std::string str = id1.to_string();
    TEST_CHECK(str.size() == 36);

    auto parsed = UUID::from_string(str);
    TEST_CHECK(parsed.has_value());
    TEST_CHECK(*parsed == id1);

    UUID id2 = UUIDGenerator::v7();
    TEST_CHECK(id2.version() == 7);
    TEST_CHECK(id2.timestamp_ms() > 0);

    // Glaze JSON roundtrip
    std::string json;
    auto ec = glz::write_json(id2, json);
    TEST_CHECK(!ec);
    TEST_CHECK(json == "\"" + id2.to_string() + "\"");

    UUID read_id{};
    auto rec = glz::read_json(read_id, json);
    TEST_CHECK(!rec);
    TEST_CHECK(read_id == id2);

    std::cout << "  -> PASS: UUID functions cleanly.\n";
}

void test_date_time() {
    std::cout << "[Test 2] Testing DateTime (UTC timestamp, ISO 8601, offsets, Glaze)...\n";
    DateTime now = DateTime::now();
    TEST_CHECK(now.epoch_micros() > 0);

    std::string iso = now.to_iso8601();
    TEST_CHECK(iso.size() == 27);
    TEST_CHECK(iso.ends_with('Z'));

    auto parsed = DateTime::from_string(iso);
    TEST_CHECK(parsed.has_value());
    TEST_CHECK(parsed->epoch_micros() == now.epoch_micros());

    // Timezone offset parsing (+05:30)
    std::string with_offset = "2026-09-07T15:30:00.000000+05:30";
    auto parsed_tz = DateTime::from_string(with_offset);
    TEST_CHECK(parsed_tz.has_value());
    // 15:30:00 +05:30 is 10:00:00 UTC
    std::string utc_iso = parsed_tz->to_iso8601();
    TEST_CHECK(utc_iso.find("10:00:00") != std::string::npos);

    // Duration arithmetic
    DateTime future = now + std::chrono::hours(24);
    TEST_CHECK(future > now);
    auto diff = future - now;
    TEST_CHECK(std::chrono::duration_cast<std::chrono::hours>(diff).count() == 24);

    // Glaze roundtrip
    std::string json;
    auto wec = glz::write_json(now, json);
    TEST_CHECK(!wec);
    TEST_CHECK(json == "\"" + iso + "\"");

    DateTime read_dt{};
    auto rec = glz::read_json(read_dt, json);
    TEST_CHECK(!rec);
    TEST_CHECK(read_dt == now);

    std::cout << "  -> PASS: DateTime ISO 8601 and timezone conversions verified.\n";
}

void test_date() {
    std::cout << "[Test 3] Testing Date (Calendar Date YYYY-MM-DD, Glaze)...\n";
    Date d(2026, 9, 7);
    TEST_CHECK(d.year() == 2026);
    TEST_CHECK(d.month() == 9);
    TEST_CHECK(d.day() == 7);
    TEST_CHECK(!d.is_leap_year());
    TEST_CHECK(d.to_string() == "2026-09-07");

    Date leap(2024, 2, 29);
    TEST_CHECK(leap.is_leap_year());
    TEST_CHECK(leap.to_string() == "2024-02-29");

    auto parsed = Date::from_string("2026-09-07");
    TEST_CHECK(parsed.has_value());
    TEST_CHECK(*parsed == d);

    // Glaze roundtrip
    std::string json;
    auto wec = glz::write_json(d, json);
    TEST_CHECK(!wec);
    TEST_CHECK(json == "\"2026-09-07\"");

    Date read_d{};
    auto rec = glz::read_json(read_d, json);
    TEST_CHECK(!rec);
    TEST_CHECK(read_d == d);

    std::cout << "  -> PASS: Date operations verified.\n";
}

void test_time() {
    std::cout << "[Test 4] Testing Time (Wall-clock HH:MM:SS.ffffff, Glaze)...\n";
    Time t(14, 30, 45, 123456);
    TEST_CHECK(t.hour() == 14);
    TEST_CHECK(t.minute() == 30);
    TEST_CHECK(t.second() == 45);
    TEST_CHECK(t.microsecond() == 123456);
    TEST_CHECK(t.to_string() == "14:30:45.123456");
    TEST_CHECK(t.to_string(false) == "14:30:45");

    auto parsed = Time::from_string("14:30:45.123456");
    TEST_CHECK(parsed.has_value());
    TEST_CHECK(*parsed == t);

    // Glaze roundtrip
    std::string json;
    auto wec = glz::write_json(t, json);
    TEST_CHECK(!wec);
    TEST_CHECK(json == "\"14:30:45.123456\"");

    Time read_t{};
    auto rec = glz::read_json(read_t, json);
    TEST_CHECK(!rec);
    TEST_CHECK(read_t == t);

    std::cout << "  -> PASS: Time operations verified.\n";
}

void test_decimal() {
    std::cout << "[Test 5] Testing Decimal<18, 4> (Exact 128-bit Fixed-Point Arithmetic)...\n";
    Decimal<18, 2> a("0.10");
    Decimal<18, 2> b("0.20");
    Decimal<18, 2> sum = a + b;
    TEST_CHECK(sum.to_string() == "0.30"); // 0.1 + 0.2 == 0.3 without binary floating point bug!

    Decimal128 price("19.9900");
    Decimal128 tax_rate("0.0825");
    Decimal128 tax = price * tax_rate;
    // 19.99 * 0.0825 = 1.649175 -> scaled to 4 decimals = 1.6491
    TEST_CHECK(tax.to_string() == "1.6491");

    Decimal128 zero("0.0000");
    TEST_CHECK(zero.is_zero());
    Decimal128 neg("-42.5000");
    TEST_CHECK(neg.is_negative());
    TEST_CHECK((-neg).is_positive());

    // Glaze roundtrip
    std::string json;
    auto wec = glz::write_json(price, json);
    TEST_CHECK(!wec);
    TEST_CHECK(json == "\"19.9900\"");

    Decimal128 read_dec{};
    auto rec = glz::read_json(read_dec, json);
    TEST_CHECK(!rec);
    TEST_CHECK(read_dec == price);

    std::cout << "  -> PASS: Decimal exact fixed-point arithmetic verified.\n";
}

void test_json() {
    std::cout << "[Test 6] Testing Json (Validated JSON Column & Lazy Glaze Access)...\n";
    InnerDTO inner{.role = "admin", .level = 10};
    Json j = Json::from(inner);
    TEST_CHECK(!j.empty());
    TEST_CHECK(j.str().find("\"role\":\"admin\"") != std::string::npos);

    auto parsed_inner = j.get<InnerDTO>();
    TEST_CHECK(parsed_inner.has_value());
    TEST_CHECK(parsed_inner->role == "admin");
    TEST_CHECK(parsed_inner->level == 10);

    ParentDTO parent{.title = "Aegon Service", .config = j};
    std::string out_json;
    auto wec = glz::write_json(parent, out_json);
    TEST_CHECK(!wec);
    TEST_CHECK(out_json.find("\"title\":\"Aegon Service\"") != std::string::npos);
    TEST_CHECK(out_json.find("\"config\":{\"role\":\"admin\",\"level\":10}") != std::string::npos);

    ParentDTO read_parent{};
    auto rec = glz::read_json(read_parent, out_json);
    TEST_CHECK(!rec);
    TEST_CHECK(read_parent.title == "Aegon Service");
    auto parsed_config = read_parent.config.get<InnerDTO>();
    TEST_CHECK(parsed_config.has_value());
    TEST_CHECK(parsed_config->role == "admin");

    std::cout << "  -> PASS: Json raw unquoted embedding verified.\n";
}

void test_ip_address() {
    std::cout << "[Test 7] Testing IpAddress (IPv4, IPv6, Loopback, Private, CIDR Subnet)...\n";
    // IPv4
    auto ip4 = IpAddress::from_string("192.168.1.50");
    TEST_CHECK(ip4.has_value());
    TEST_CHECK(ip4->is_ipv4());
    TEST_CHECK(!ip4->is_loopback());
    TEST_CHECK(ip4->is_private());
    TEST_CHECK(ip4->to_string() == "192.168.1.50");

    // CIDR subnet checks
    TEST_CHECK(ip4->in_subnet("192.168.1.0/24"));
    TEST_CHECK(ip4->in_subnet("192.168.0.0/16"));
    TEST_CHECK(!ip4->in_subnet("10.0.0.0/8"));

    // Loopback
    auto loop4 = IpAddress::from_string("127.0.0.1");
    TEST_CHECK(loop4->is_loopback());

    // IPv6
    auto ip6 = IpAddress::from_string("2001:db8::1");
    TEST_CHECK(ip6.has_value());
    TEST_CHECK(ip6->is_ipv6());
    TEST_CHECK(ip6->in_subnet("2001:db8::/32"));
    TEST_CHECK(!ip6->in_subnet("2001:db8:1::/48"));

    auto loop6 = IpAddress::from_string("::1");
    TEST_CHECK(loop6->is_loopback());

    // Glaze roundtrip
    std::string json;
    auto wec = glz::write_json(*ip4, json);
    TEST_CHECK(!wec);
    TEST_CHECK(json == "\"192.168.1.50\"");

    IpAddress read_ip{};
    auto rec = glz::read_json(read_ip, json);
    TEST_CHECK(!rec);
    TEST_CHECK(read_ip == *ip4);

    std::cout << "  -> PASS: IpAddress operations and CIDR checks verified.\n";
}

void test_mac_address() {
    std::cout << "[Test 8] Testing MacAddress (48-bit IEEE 802, Glaze)...\n";
    auto mac = MacAddress::from_string("00:1a:2b:3c:4d:5e");
    TEST_CHECK(mac.has_value());
    TEST_CHECK(mac->to_string() == "00:1a:2b:3c:4d:5e");

    // Glaze roundtrip
    std::string json;
    auto wec = glz::write_json(*mac, json);
    TEST_CHECK(!wec);
    TEST_CHECK(json == "\"00:1a:2b:3c:4d:5e\"");

    MacAddress read_mac{};
    auto rec = glz::read_json(read_mac, json);
    TEST_CHECK(!rec);
    TEST_CHECK(read_mac == *mac);

    std::cout << "  -> PASS: MacAddress operations verified.\n";
}

void test_blob() {
    std::cout << "[Test 9] Testing Blob (Binary Buffer, Base64 JSON, Glaze)...\n";
    std::string raw_data = "Hello, Aegon C++26 Data Types!";
    Blob b(reinterpret_cast<const uint8_t*>(raw_data.data()), raw_data.size());
    TEST_CHECK(b.size() == raw_data.size());

    std::string b64 = b.to_base64();
    auto dec = Blob::from_base64(b64);
    TEST_CHECK(dec.has_value());
    TEST_CHECK(dec->bytes() == b.bytes());

    // Glaze roundtrip (serializes as Base64 string in JSON)
    std::string json;
    auto wec = glz::write_json(b, json);
    TEST_CHECK(!wec);
    TEST_CHECK(json == "\"" + b64 + "\"");

    Blob read_b{};
    auto rec = glz::read_json(read_b, json);
    TEST_CHECK(!rec);
    TEST_CHECK(read_b == b);

    std::cout << "  -> PASS: Blob Base64 serialization verified.\n";
}

void test_hash256() {
    std::cout << "[Test 10] Testing Hash256 (32-byte Cryptographic Token, Constant-time cmp, Glaze)...\n";
    std::string hex_str = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    auto h = Hash256::from_hex(hex_str);
    TEST_CHECK(h.has_value());
    TEST_CHECK(h->to_hex() == hex_str);

    auto h_copy = *h;
    TEST_CHECK(h_copy == *h); // Constant-time equality check

    // Glaze roundtrip
    std::string json;
    auto wec = glz::write_json(*h, json);
    TEST_CHECK(!wec);
    TEST_CHECK(json == "\"" + hex_str + "\"");

    Hash256 read_h{};
    auto rec = glz::read_json(read_h, json);
    TEST_CHECK(!rec);
    TEST_CHECK(read_h == *h);

    std::cout << "  -> PASS: Hash256 constant-time comparison and hex conversions verified.\n";
}

void test_full_enterprise_dto() {
    std::cout << "[Test 11] Testing Full Enterprise DTO with all foundational data types...\n";

    EnterpriseRecordDTO record{
        .id = UUIDGenerator::v7(),
        .title = "Core Infrastructure Contract",
        .created_at = DateTime::now(),
        .effective_date = Date(2026, 9, 7),
        .shift_start = Time(9, 0, 0),
        .balance = Decimal128("1250000.5000"),
        .metadata = Json("{\"tier\":\"enterprise\",\"cluster\":\"eu-west-1\"}"),
        .server_ip = *IpAddress::from_string("10.240.0.1"),
        .hardware_mac = *MacAddress::from_string("00:11:22:33:44:55"),
        .payload = Blob(reinterpret_cast<const uint8_t*>("secure_payload"), 14),
        .sha256_checksum = *Hash256::from_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
    };

    // 1. Validate DTO
    ValidationRules v;
    record.validate(v);
    TEST_CHECK(!v.has_violations());

    // 2. Serialize DTO to JSON
    std::string json;
    auto ec = glz::write_json(record, json);
    TEST_CHECK(!ec);
    TEST_CHECK(json.find("\"title\":\"Core Infrastructure Contract\"") != std::string::npos);
    TEST_CHECK(json.find("\"balance\":\"1250000.5000\"") != std::string::npos);
    TEST_CHECK(json.find("\"server_ip\":\"10.240.0.1\"") != std::string::npos);
    TEST_CHECK(json.find("\"hardware_mac\":\"00:11:22:33:44:55\"") != std::string::npos);
    TEST_CHECK(json.find("\"cluster\":\"eu-west-1\"") != std::string::npos);

    // 3. Deserialize back into clean DTO
    EnterpriseRecordDTO read_record{};
    auto rec = glz::read_json(read_record, json);
    TEST_CHECK(!rec);
    TEST_CHECK(read_record.id == record.id);
    TEST_CHECK(read_record.title == record.title);
    TEST_CHECK(read_record.balance == record.balance);
    TEST_CHECK(read_record.effective_date == record.effective_date);
    TEST_CHECK(read_record.shift_start == record.shift_start);
    TEST_CHECK(read_record.server_ip == record.server_ip);
    TEST_CHECK(read_record.hardware_mac == record.hardware_mac);
    TEST_CHECK(read_record.sha256_checksum == record.sha256_checksum);

    std::cout << "  -> PASS: All foundational types seamlessly bound, validated, and serialized.\n";
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "     AEGON C++26 FOUNDATIONAL DATA TYPES TEST SUITE    \n";
    std::cout << "=======================================================\n\n";

    test_uuid();
    test_date_time();
    test_date();
    test_time();
    test_decimal();
    test_json();
    test_ip_address();
    test_mac_address();
    test_blob();
    test_hash256();
    test_full_enterprise_dto();

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL FOUNDATIONAL DATA TYPES TESTS PASSED! <<<\n";
    std::cout << "=======================================================\n\n";
    return 0;
}
