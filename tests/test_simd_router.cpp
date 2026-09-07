#include "http/Router.h"
#include "http/SimdRouter.h"
#include "http/Request.h"
#include "http/Response.h"
#include "http/Context.h"
#include <iostream>
#include <cassert>
#include <string>
#include <chrono>
#include <vector>

#define TEST_CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace aegon;
using namespace aegon::http;
using namespace aegon::http::simd;

void test_simd_common_prefix() {
    std::cout << "[Test 1] Testing SIMD Common Prefix Length across all vector boundaries...\n";

    // 1. Empty strings
    TEST_CHECK(simd_common_prefix("", "") == 0);
    TEST_CHECK(simd_common_prefix("abc", "") == 0);
    TEST_CHECK(simd_common_prefix("", "abc") == 0);

    // 2. Small strings (< 8 bytes)
    TEST_CHECK(simd_common_prefix("a", "a") == 1);
    TEST_CHECK(simd_common_prefix("ab", "ac") == 1);
    TEST_CHECK(simd_common_prefix("hello", "hello") == 5);
    TEST_CHECK(simd_common_prefix("help", "hell") == 3);

    // 3. 8 to 15 bytes (64-bit word boundary)
    TEST_CHECK(simd_common_prefix("12345678", "12345678") == 8);
    TEST_CHECK(simd_common_prefix("12345678A", "12345678B") == 8);
    TEST_CHECK(simd_common_prefix("/api/v1/auth", "/api/v1/auth") == 12);
    TEST_CHECK(simd_common_prefix("/api/v1/users", "/api/v1/orders") == 8);

    // 4. 16 to 31 bytes (128-bit SSE boundary)
    std::string s16_a = "0123456789abcdef";
    std::string s16_b = "0123456789abcdef";
    TEST_CHECK(simd_common_prefix(s16_a, s16_b) == 16);
    TEST_CHECK(simd_common_prefix(s16_a + "X", s16_b + "Y") == 16);

    std::string s24_a = "/api/v1/organizations/members";
    std::string s24_b = "/api/v1/organizations/billing";
    TEST_CHECK(simd_common_prefix(s24_a, s24_b) == 22); // "/api/v1/organizations/" is 22 chars

    // 5. >= 32 bytes (256-bit AVX2 / AVX-512 boundary)
    std::string s32_a = "12345678901234567890123456789012";
    std::string s32_b = "12345678901234567890123456789012";
    TEST_CHECK(simd_common_prefix(s32_a, s32_b) == 32);

    std::string s64_a = "/api/v1/service/cluster/node/compute/datacenter/rack/chassis/blade/42";
    std::string s64_b = "/api/v1/service/cluster/node/compute/datacenter/rack/chassis/blade/99";
    TEST_CHECK(simd_common_prefix(s64_a, s64_b) == (s64_a.rfind('/') + 1));

    // Mismatch at byte 31
    std::string m31_a = "0123456789012345678901234567890A_tail";
    std::string m31_b = "0123456789012345678901234567890B_tail";
    TEST_CHECK(simd_common_prefix(m31_a, m31_b) == 31);

    // Mismatch at byte 32
    std::string m32_a = "01234567890123456789012345678901A_tail";
    std::string m32_b = "01234567890123456789012345678901B_tail";
    TEST_CHECK(simd_common_prefix(m32_a, m32_b) == 32);

    std::cout << "  -> PASS: SIMD common prefix verified across 1, 8, 16, 32, 64+ byte boundaries!\n";
}

void test_simd_find_char() {
    std::cout << "[Test 2] Testing SIMD Vectorized Delimiter Search (simd_find_char)...\n";

    TEST_CHECK(simd_find_char("", '/') == std::string_view::npos);
    TEST_CHECK(simd_find_char("/", '/') == 0);
    TEST_CHECK(simd_find_char("abc", '/') == std::string_view::npos);
    TEST_CHECK(simd_find_char("abc/def", '/') == 3);

    // 16-byte boundary
    std::string s16 = "0123456789abcde/xyz";
    TEST_CHECK(simd_find_char(s16, '/') == 15);

    // 32-byte boundary
    std::string s32 = "0123456789012345678901234567890/rest";
    TEST_CHECK(simd_find_char(s32, '/') == 31);

    // Deep search (> 32 bytes)
    std::string s64 = "012345678901234567890123456789012345678901234567890123456789012/end";
    TEST_CHECK(simd_find_char(s64, '/') == 63);

    // Character not present in large string
    std::string no_slash = "0123456789012345678901234567890123456789012345678901234567890123456789";
    TEST_CHECK(simd_find_char(no_slash, '/') == std::string_view::npos);

    std::cout << "  -> PASS: SIMD character search verified across all offsets.\n";
}

void test_simd_starts_with() {
    std::cout << "[Test 3] Testing SIMD Vectorized Starts-With (simd_starts_with)...\n";

    TEST_CHECK(simd_starts_with("/api/users", "/api"));
    TEST_CHECK(simd_starts_with("/api/users", "/"));
    TEST_CHECK(!simd_starts_with("/api", "/api/users"));
    TEST_CHECK(simd_starts_with("exact_match", "exact_match"));
    TEST_CHECK(!simd_starts_with("exact_match", "exact_mism"));

    std::string long_path = "/v1/very/long/enterprise/microservice/endpoint/resource/path";
    TEST_CHECK(simd_starts_with(long_path, "/v1/very/long/enterprise/microservice/endpoint"));
    TEST_CHECK(!simd_starts_with(long_path, "/v1/very/long/enterprise/microservice/database"));

    std::cout << "  -> PASS: SIMD starts_with verified.\n";
}

void test_hybrid_static_routes_fast_path() {
    std::cout << "[Test 4] Testing Hybrid Architecture: O(1) Static Hash Map + Radix Tree...\n";

    Router router;

    // Register Static Routes
    router.get("/", [](Context& ctx) { ctx.res().text("root"); });
    router.get("/health", [](Context& ctx) { ctx.res().text("healthy"); });
    router.get("/api/v1/metrics", [](Context& ctx) { ctx.res().text("metrics"); });
    router.post("/api/v1/login", [](Context& ctx) { ctx.res().text("logged_in"); });

    // Register Dynamic Routes
    router.get("/users/:id", [](Context& ctx) {
        ctx.res().text(std::string(ctx.req().param("id").value_or("")));
    });
    router.get("/files/*filepath", [](Context& ctx) {
        ctx.res().text(std::string(ctx.req().param("filepath").value_or("")));
    });

    // Verify static route count: only the 4 static routes are indexed in the O(1) table
    TEST_CHECK(router.static_route_count() == 4);

    // 1. Static Route Match: GET /health
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/health");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        TEST_CHECK(!res.method_not_allowed);
        Response resp;
        Context ctx(req, resp);
        auto t = (*res.handler)(ctx);
        t.resume();
        TEST_CHECK(resp.body() == "healthy");
    }

    // 2. Static Route Trailing Slash Tolerance: GET /health/
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/health/");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        auto t = (*res.handler)(ctx);
        t.resume();
        TEST_CHECK(resp.body() == "healthy");
    }

    // 3. Static Route Method Not Allowed (405): POST /health
    {
        Request req;
        req.set_method(Method::POST);
        req.set_path("/health");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        TEST_CHECK(res.method_not_allowed);
        TEST_CHECK(res.handler == nullptr);
    }

    // 4. Dynamic Route Match: GET /users/42 (falls through to SIMD Radix Tree)
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/users/42");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        TEST_CHECK(!res.method_not_allowed);
        TEST_CHECK(req.param("id") == "42");
        Response resp;
        Context ctx(req, resp);
        auto t = (*res.handler)(ctx);
        t.resume();
        TEST_CHECK(resp.body() == "42");
    }

    // 5. Wildcard Dynamic Route Match: GET /files/images/banner.png
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/files/images/banner.png");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        TEST_CHECK(req.param("filepath") == "images/banner.png");
    }

    // 6. Unknown Route: GET /not_found
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/not_found");
        auto res = router.match(req);
        TEST_CHECK(!res.route_found);
    }

    std::cout << "  -> PASS: Hybrid routing verified with O(1) static fast-path & dynamic fallback!\n";
}

void test_benchmark_routing_speed() {
    std::cout << "[Test 5] Running Dispatch Microbenchmark (1,000,000 iterations)...\n";

    Router router;
    router.get("/", [](Context&) {});
    router.get("/health", [](Context&) {});
    router.get("/api/v1/auth/login", [](Context&) {});
    router.get("/api/v1/users", [](Context&) {});
    router.get("/api/v1/users/:id", [](Context&) {});
    router.get("/api/v1/users/:user_id/posts/:post_id", [](Context&) {});

    constexpr size_t ITERATIONS = 1'000'000;

    // Benchmark 1: Static Route O(1) Fast Path (/api/v1/users)
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/api/v1/users");

        auto start = std::chrono::high_resolution_clock::now();
        size_t matches = 0;
        for (size_t i = 0; i < ITERATIONS; ++i) {
            auto res = router.match(req);
            if (res.route_found) ++matches;
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        double avg_ns = static_cast<double>(ns) / ITERATIONS;
        double ops_sec = (static_cast<double>(ITERATIONS) / ns) * 1e9;

        TEST_CHECK(matches == ITERATIONS);
        std::cout << "  -> [Static Fast Path] " << avg_ns << " ns/op | "
                  << static_cast<size_t>(ops_sec / 1'000'000.0) << " Million routes/sec\n";
    }

    // Benchmark 2: Dynamic Parameterized Route via SIMD Radix Tree (/api/v1/users/42/posts/100)
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/api/v1/users/42/posts/100");

        auto start = std::chrono::high_resolution_clock::now();
        size_t matches = 0;
        for (size_t i = 0; i < ITERATIONS; ++i) {
            req.clear_params();
            auto res = router.match(req);
            if (res.route_found) ++matches;
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        double avg_ns = static_cast<double>(ns) / ITERATIONS;
        double ops_sec = (static_cast<double>(ITERATIONS) / ns) * 1e9;

        TEST_CHECK(matches == ITERATIONS);
        std::cout << "  -> [Dynamic SIMD Radix] " << avg_ns << " ns/op | "
                  << static_cast<size_t>(ops_sec / 1'000'000.0) << " Million routes/sec\n";
    }

    std::cout << "  -> PASS: Microbenchmark completed.\n";
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "   AEGON SIMD & HYBRID STATIC ROUTER TEST SUITE        \n";
    std::cout << "   (O(1) Hash Map + SIMD AVX-512/AVX2 Radix Tree)      \n";
    std::cout << "=======================================================\n\n";

    test_simd_common_prefix();
    test_simd_find_char();
    test_simd_starts_with();
    test_hybrid_static_routes_fast_path();
    test_benchmark_routing_speed();

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL SIMD & HYBRID ROUTER TESTS PASSED! <<<      \n";
    std::cout << "=======================================================\n\n";
    return 0;
}
