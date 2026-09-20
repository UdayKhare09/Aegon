#include "data/types/UUID.h"
#include "data/types/UUIDGenerator.h"

#include <iostream>
#include <vector>
#include <unordered_set>
#include <chrono>
#include <cstdlib>
#include <iomanip>

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: (" #cond ") - " << (msg) << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort(); \
        } \
    } while (0)


using namespace aegon::data;

void test_v4_properties() {
    std::cout << "[TEST] Running UUID v4 properties and uniqueness test..." << std::endl;
    std::unordered_set<UUID> seen;
    const size_t COUNT = 100'000;
    seen.reserve(COUNT);

    for (size_t i = 0; i < COUNT; ++i) {
        UUID id = UUIDGenerator::v4();
        TEST_CHECK(id.version() == 4, "UUID version must be 4");
        TEST_CHECK(id.variant() == 1, "UUID variant must be 1 (RFC 4122 / 9562)");
        TEST_CHECK(!id.is_nil(), "UUID v4 must not be nil");
        seen.insert(id);
    }

    TEST_CHECK(seen.size() == COUNT, "All 100,000 generated v4 UUIDs must be unique");
    std::cout << "  -> PASS: 100,000 UUID v4 generated, all unique with valid version/variant.\n";
}

void test_v7_properties_and_monotonicity() {
    std::cout << "[TEST] Running UUID v7 timestamp and monotonic ordering test..." << std::endl;
    const size_t COUNT = 100'000;
    std::vector<UUID> ids(COUNT);

    auto start_wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    for (size_t i = 0; i < COUNT; ++i) {
        ids[i] = UUIDGenerator::v7();
    }

    auto end_wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // Verify properties
    for (size_t i = 0; i < COUNT; ++i) {
        TEST_CHECK(ids[i].version() == 7, "UUID version must be 7");
        TEST_CHECK(ids[i].variant() == 1, "UUID variant must be 1 (RFC 9562)");

        uint64_t ts = ids[i].timestamp_ms();
        TEST_CHECK(ts >= static_cast<uint64_t>(start_wall_ms) && ts <= static_cast<uint64_t>(end_wall_ms + 1000),
                   "UUID v7 timestamp must reflect current epoch ms");

        if (i > 0) {
            // Strict monotonic ordering: each UUID must be strictly greater than the preceding one
            bool is_less = (ids[i - 1] < ids[i]);
            TEST_CHECK(is_less, "UUID v7 must be strictly monotonically increasing!");

            // String representation must also sort identically
            std::string s_prev = ids[i - 1].to_string();
            std::string s_curr = ids[i].to_string();
            TEST_CHECK(s_prev < s_curr, "String representation of v7 must preserve monotonic ordering!");
        }
    }

    std::cout << "  -> PASS: 100,000 UUID v7 generated. Strict monotonicity verified.\n";
}

void test_string_roundtrip_and_validation() {
    std::cout << "[TEST] Running SIMD serialization & parsing tests..." << std::endl;

    for (int i = 0; i < 50'000; ++i) {
        UUID original = (i % 2 == 0) ? UUIDGenerator::v4() : UUIDGenerator::v7();
        std::string str = original.to_string();

        TEST_CHECK(str.size() == 36, "Formatted UUID must be 36 characters");
        TEST_CHECK(str[8] == '-' && str[13] == '-' && str[18] == '-' && str[23] == '-', "Hyphens must be placed at 8, 13, 18, 23");

        auto parsed = UUID::from_string(str);
        TEST_CHECK(parsed.has_value(), "SIMD parser must successfully parse canonical string");
        TEST_CHECK(*parsed == original, "Round-tripped UUID must match original byte-for-byte");
    }

    // Test case insensitivity (uppercase hex)
    std::string uppercase_uuid = "018F6E3B-8C90-7F3A-8F4C-0123456789AB";
    auto parsed_upper = UUID::from_string(uppercase_uuid);
    TEST_CHECK(parsed_upper.has_value(), "Parser must support uppercase hex");
    TEST_CHECK(parsed_upper->version() == 7, "Parsed uppercase version must match");

    // Test invalid strings rejection
    TEST_CHECK(!UUID::from_string("").has_value(), "Empty string should fail");
    TEST_CHECK(!UUID::from_string("018f6e3b-8c90-7f3a-8f4c-0123456789a").has_value(), "35 chars should fail");
    TEST_CHECK(!UUID::from_string("018f6e3b-8c90-7f3a-8f4c-0123456789abc").has_value(), "37 chars should fail");
    TEST_CHECK(!UUID::from_string("018f6e3b08c90-7f3a-8f4c-0123456789ab").has_value(), "Missing hyphen at 8 should fail");
    TEST_CHECK(!UUID::from_string("018f6e3b-8c90x7f3a-8f4c-0123456789ab").has_value(), "Missing hyphen at 13 should fail");
    TEST_CHECK(!UUID::from_string("018f6e3b-8c90-7f3a-8f4c-0123456789zz").has_value(), "Invalid hex chars 'z' should fail");
    TEST_CHECK(!UUID::from_string("018f6e3b-8c90-7f3a-8f4c-0123456789@#").has_value(), "Invalid symbols should fail");

    std::cout << "  -> PASS: SIMD serialization, parsing, and error handling fully verified.\n";
}

void test_batch_generation() {
    std::cout << "[TEST] Running AVX-512 batch generation test..." << std::endl;
    std::vector<UUID> batch(4096);
    UUIDGenerator::v4_batch(batch);

    for (const auto& id : batch) {
        TEST_CHECK(id.version() == 4, "Batch UUID version must be 4");
        TEST_CHECK(id.variant() == 1, "Batch UUID variant must be 1");
    }
    std::cout << "  -> PASS: AVX-512 batch generation validated.\n";
}

void run_benchmarks() {
    std::cout << "\n=======================================================\n";
    std::cout << "               CPU BENCHMARKS (ZEN 4)                  \n";
    std::cout << "=======================================================\n";

    const size_t ITERS = 10'000'000;
    std::vector<UUID> storage(100'000);

    // 1. UUID v4 Throughput
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < ITERS; ++i) {
            UUID id = UUIDGenerator::v4();
            storage[i % storage.size()] = id;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        double ops = ITERS / sec;
        double ns_per_op = (sec * 1e9) / ITERS;
        std::cout << "  UUID v4 Generation  : " << std::fixed << std::setprecision(2)
                  << (ops / 1e6) << " M ops/sec  (" << ns_per_op << " ns/op)\n";
    }

    // 2. UUID v4 Batch Throughput (AVX-512)
    {
        const size_t BATCH_ITERS = 10'000'000;
        std::vector<UUID> batch(1024);
        auto t0 = std::chrono::high_resolution_clock::now();
        size_t generated = 0;
        while (generated < BATCH_ITERS) {
            UUIDGenerator::v4_batch(batch);
            generated += batch.size();
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        double ops = generated / sec;
        double ns_per_op = (sec * 1e9) / generated;
        std::cout << "  UUID v4 Batch (AVX) : " << std::fixed << std::setprecision(2)
                  << (ops / 1e6) << " M ops/sec  (" << ns_per_op << " ns/op)\n";
    }

    // 3. UUID v7 Single Throughput
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < ITERS; ++i) {
            UUID id = UUIDGenerator::v7();
            storage[i % storage.size()] = id;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        double ops = ITERS / sec;
        double ns_per_op = (sec * 1e9) / ITERS;
        std::cout << "  UUID v7 Single      : " << std::fixed << std::setprecision(2)
                  << (ops / 1e6) << " M ops/sec  (" << ns_per_op << " ns/op)\n";
    }

    // 4. UUID v7 Batch Throughput (amortized single clock read)
    {
        const size_t BATCH_ITERS = 10'000'000;
        std::vector<UUID> batch(1024);
        auto t0 = std::chrono::high_resolution_clock::now();
        size_t generated = 0;
        while (generated < BATCH_ITERS) {
            UUIDGenerator::v7_batch(batch);
            generated += batch.size();
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        double ops = generated / sec;
        double ns_per_op = (sec * 1e9) / generated;
        std::cout << "  UUID v7 Batch       : " << std::fixed << std::setprecision(2)
                  << (ops / 1e6) << " M ops/sec  (" << ns_per_op << " ns/op)\n";
    }

    // 4. SIMD to_chars / to_string Throughput
    {
        UUID sample = UUIDGenerator::v7();
        char buf[64];
        const size_t STR_ITERS = 20'000'000;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < STR_ITERS; ++i) {
            sample.to_chars(buf);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        double ops = STR_ITERS / sec;
        double ns_per_op = (sec * 1e9) / STR_ITERS;
        std::cout << "  SIMD to_chars       : " << std::fixed << std::setprecision(2)
                  << (ops / 1e6) << " M ops/sec  (" << ns_per_op << " ns/op)\n";
    }

    // 5. SIMD from_string Throughput
    {
        std::string canonical = UUIDGenerator::v7().to_string();
        const size_t PARSE_ITERS = 20'000'000;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < PARSE_ITERS; ++i) {
            auto parsed = UUID::from_string(canonical);
            (void)parsed;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        double ops = PARSE_ITERS / sec;
        double ns_per_op = (sec * 1e9) / PARSE_ITERS;
        std::cout << "  SIMD from_string    : " << std::fixed << std::setprecision(2)
                  << (ops / 1e6) << " M ops/sec  (" << ns_per_op << " ns/op)\n";
    }
    std::cout << "=======================================================\n";
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << "       AEGON HARDWARE-ACCELERATED UUID TEST SUITE      \n";
    std::cout << "=======================================================\n\n";

    test_v4_properties();
    test_v7_properties_and_monotonicity();
    test_string_roundtrip_and_validation();
    test_batch_generation();
    run_benchmarks();

    std::cout << "\nALL TESTS PASSED SUCCESSFULLY.\n";
    return 0;
}
