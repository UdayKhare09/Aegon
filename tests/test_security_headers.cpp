#include "http/Server.h"
#include "http/middleware/SecurityHeaders.h"
#include "http/middleware/Cors.h"
#include "http/v1/Http1Parser.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

using namespace aegon;
using namespace aegon::http;
using namespace aegon::http::middleware;

static Response dispatch_offline(Router& router, std::string_view raw_req) {
    Request req;
    size_t consumed = 0;
    std::string s(raw_req);
    v1::Http1Parser::parse(s, req, consumed);
    Response res;
    auto task = router.dispatch(req, res, nullptr);
    task.resume();
    return res;
}

// 1. Defaults preset verification (OWASP baseline)
void test_1_defaults_preset() {
    std::cout << "[TEST 1] Testing SecurityHeadersConfig::defaults() preset...\n";
    Router router;

    router.use(security_headers(SecurityHeadersConfig::defaults()));
    router.get("/test", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("secure");
    });

    Response res = dispatch_offline(router, "GET /test HTTP/1.1\r\nHost: localhost\r\n\r\n");

    assert(res.status() == StatusCode::Ok);
    assert(res.headers().get("x-content-type-options") == "nosniff");
    assert(res.headers().get("x-frame-options") == "DENY");
    assert(res.headers().get("x-xss-protection") == "0");
    assert(res.headers().get("referrer-policy") == "strict-origin-when-cross-origin");
    assert(res.headers().get("strict-transport-security") == "max-age=31536000; includeSubDomains");
    assert(res.headers().get("cross-origin-opener-policy") == "same-origin");
    assert(res.headers().get("cross-origin-resource-policy") == "same-origin");
    std::cout << "  -> PASS\n";
}

// 2. API preset verification
void test_2_api_preset() {
    std::cout << "[TEST 2] Testing SecurityHeadersConfig::api() preset...\n";
    Router router;

    router.use(security_headers(SecurityHeadersConfig::api()));
    router.get("/api/v1/data", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("data");
    });

    Response res = dispatch_offline(router, "GET /api/v1/data HTTP/1.1\r\nHost: localhost\r\n\r\n");

    assert(res.status() == StatusCode::Ok);
    assert(res.headers().get("referrer-policy") == "no-referrer");
    assert(res.headers().get("x-frame-options") == "DENY");
    assert(res.headers().get("x-content-type-options") == "nosniff");
    std::cout << "  -> PASS\n";
}

// 3. Custom HSTS with preload flag
void test_3_custom_hsts_preload() {
    std::cout << "[TEST 3] Testing custom HSTS with preload...\n";
    using namespace std::chrono_literals;
    Router router;

    router.use(security_headers(SecurityHeadersConfig{
        .hsts = HstsConfig{
            .enabled = true,
            .max_age = 7200s,
            .include_subdomains = true,
            .preload = true
        }
    }));

    router.get("/hsts", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("hsts");
    });

    Response res = dispatch_offline(router, "GET /hsts HTTP/1.1\r\nHost: localhost\r\n\r\n");

    assert(res.headers().get("strict-transport-security") == "max-age=7200; includeSubDomains; preload");
    std::cout << "  -> PASS\n";
}

// 4. Disabled HSTS
void test_4_disabled_hsts() {
    std::cout << "[TEST 4] Testing disabled HSTS...\n";
    Router router;

    router.use(security_headers(SecurityHeadersConfig{
        .hsts = HstsConfig{ .enabled = false }
    }));

    router.get("/no-hsts", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("no-hsts");
    });

    Response res = dispatch_offline(router, "GET /no-hsts HTTP/1.1\r\nHost: localhost\r\n\r\n");

    assert(!res.headers().contains("strict-transport-security"));
    std::cout << "  -> PASS\n";
}

// 5. FrameOption::SameOrigin
void test_5_sameorigin_frame() {
    std::cout << "[TEST 5] Testing FrameOption::SameOrigin...\n";
    Router router;

    router.use(security_headers(SecurityHeadersConfig{
        .frame_options = FrameOption::SameOrigin
    }));

    router.get("/iframe-allowed", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("iframe");
    });

    Response res = dispatch_offline(router, "GET /iframe-allowed HTTP/1.1\r\nHost: localhost\r\n\r\n");

    assert(res.headers().get("x-frame-options") == "SAMEORIGIN");
    std::cout << "  -> PASS\n";
}

// 6. FrameOption::Disabled
void test_6_disabled_frame() {
    std::cout << "[TEST 6] Testing FrameOption::Disabled...\n";
    Router router;

    router.use(security_headers(SecurityHeadersConfig{
        .frame_options = FrameOption::Disabled
    }));

    router.get("/no-frame-header", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("none");
    });

    Response res = dispatch_offline(router, "GET /no-frame-header HTTP/1.1\r\nHost: localhost\r\n\r\n");

    assert(!res.headers().contains("x-frame-options"));
    std::cout << "  -> PASS\n";
}

// 7. Content Security Policy (CSP)
void test_7_csp() {
    std::cout << "[TEST 7] Testing Content-Security-Policy...\n";
    Router router;

    router.use(security_headers(SecurityHeadersConfig{
        .content_security_policy = "default-src 'self'; img-src https: data:"
    }));

    router.get("/csp", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("csp");
    });

    Response res = dispatch_offline(router, "GET /csp HTTP/1.1\r\nHost: localhost\r\n\r\n");

    assert(res.headers().get("content-security-policy") == "default-src 'self'; img-src https: data:");
    assert(!res.headers().contains("content-security-policy-report-only"));
    std::cout << "  -> PASS\n";
}

// 8. CSP Report-Only Mode
void test_8_csp_report_only() {
    std::cout << "[TEST 8] Testing Content-Security-Policy-Report-Only...\n";
    Router router;

    router.use(security_headers(SecurityHeadersConfig{
        .content_security_policy = "default-src 'self'",
        .csp_report_only = true
    }));

    router.get("/csp-report", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("report");
    });

    Response res = dispatch_offline(router, "GET /csp-report HTTP/1.1\r\nHost: localhost\r\n\r\n");

    assert(!res.headers().contains("content-security-policy"));
    assert(res.headers().get("content-security-policy-report-only") == "default-src 'self'");
    std::cout << "  -> PASS\n";
}

// 9. Sensitive Route Cache Prevention (Spring Security style)
void test_9_sensitive_cache_prevention() {
    std::cout << "[TEST 9] Testing Spring Security-style cache prevention...\n";
    Router router;

    router.use(security_headers(SecurityHeadersConfig{
        .prevent_browser_caching = true
    }));

    router.get("/account/balance", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("1000");
    });

    Response res = dispatch_offline(router, "GET /account/balance HTTP/1.1\r\nHost: localhost\r\n\r\n");

    assert(res.headers().get("cache-control") == "no-cache, no-store, max-age=0, must-revalidate");
    assert(res.headers().get("pragma") == "no-cache");
    assert(res.headers().get("expires") == "0");
    std::cout << "  -> PASS\n";
}

// 10. Pipeline Composition: SecurityHeaders + CORS
void test_10_combined_security_and_cors() {
    std::cout << "[TEST 10] Testing combined SecurityHeaders + CORS pipeline...\n";
    Router router;

    router.use(security_headers(SecurityHeadersConfig::defaults()))
          .use(cors(CorsConfig::permissive()));

    router.get("/combined", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("combined");
    });

    Response res = dispatch_offline(router,
        "GET /combined HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Origin: https://webapp.com\r\n\r\n");

    assert(res.status() == StatusCode::Ok);
    // Security headers present
    assert(res.headers().get("x-content-type-options") == "nosniff");
    assert(res.headers().get("x-frame-options") == "DENY");
    assert(res.headers().get("strict-transport-security") == "max-age=31536000; includeSubDomains");

    // CORS headers present
    assert(res.headers().get("access-control-allow-origin") == "*");
    std::cout << "  -> PASS\n";
}

// 11. DoS Header count limit verification (max_header_count)
void test_11_max_header_count_limit() {
    std::cout << "[TEST 11] Testing max_header_count DoS mitigation...\n";
    ProtocolLimits limits;
    limits.max_header_count = 3;

    // 1. Request with 4 headers (exceeds limit of 3) -> should fail
    {
        std::string raw =
            "GET /test HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "X-Header-1: value1\r\n"
            "X-Header-2: value2\r\n"
            "X-Header-3: value3\r\n\r\n";
        Request req;
        size_t consumed = 0;
        auto st = v1::Http1Parser::parse(raw, req, consumed, limits);
        assert(st == v1::ParseStatus::HeadersTooLarge);
    }

    // 2. Request with 2 headers (within limit of 3) -> should pass
    {
        std::string raw =
            "GET /test HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "X-Header-1: value1\r\n\r\n";
        Request req;
        size_t consumed = 0;
        auto st = v1::Http1Parser::parse(raw, req, consumed, limits);
        assert(st == v1::ParseStatus::Complete);
    }
    std::cout << "  -> PASS\n";
}

// 12. Path Traversal & Canonicalization defense verification
void test_12_path_traversal_guards() {
    std::cout << "[TEST 12] Testing path traversal & canonicalization defense...\n";
    namespace fs = std::filesystem;
    fs::path temp_root = fs::temp_directory_path() / "aegon_security_test_dir";
    fs::create_directories(temp_root / "subdir");

    // Create safe file inside root
    std::ofstream(temp_root / "safe.txt") << "safe content";
    // Create secret outside root
    std::ofstream(temp_root.parent_path() / "outside_secret.txt") << "secret!";

    Router router;
    router.static_files("/static", temp_root.string());

    // 1. Safe access
    {
        Response res = dispatch_offline(router, "GET /static/safe.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
    }

    // 2. Standard dot-dot breakout
    {
        Response res = dispatch_offline(router, "GET /static/../outside_secret.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
        assert(res.status() == StatusCode::NotFound);
    }

    // 3. Subdirectory dot-dot breakout
    {
        Response res = dispatch_offline(router, "GET /static/subdir/../../outside_secret.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
        assert(res.status() == StatusCode::NotFound);
    }

    // 4. URL-encoded dot-dot breakout (%2e%2e)
    {
        Response res = dispatch_offline(router, "GET /static/%2e%2e/outside_secret.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
        assert(res.status() == StatusCode::NotFound);
    }

    // 5. Mixed URL-encoded breakout (%2e%2e%2f)
    {
        Response res = dispatch_offline(router, "GET /static/%2e%2e%2foutside_secret.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
        assert(res.status() == StatusCode::NotFound);
    }

    // 6. Backslash traversal
    {
        Response res = dispatch_offline(router, "GET /static/..\\outside_secret.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
        assert(res.status() == StatusCode::NotFound);
    }

    // Clean up test files
    fs::remove_all(temp_root);
    fs::remove(temp_root.parent_path() / "outside_secret.txt");

    std::cout << "  -> PASS\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   RUNNING SECURITY HEADERS TEST SUITE  \n";
    std::cout << "========================================\n";

    test_1_defaults_preset();
    test_2_api_preset();
    test_3_custom_hsts_preload();
    test_4_disabled_hsts();
    test_5_sameorigin_frame();
    test_6_disabled_frame();
    test_7_csp();
    test_8_csp_report_only();
    test_9_sensitive_cache_prevention();
    test_10_combined_security_and_cors();
    test_11_max_header_count_limit();
    test_12_path_traversal_guards();

    std::cout << "========================================\n";
    std::cout << " ALL 12 SECURITY TESTS PASSED!          \n";
    std::cout << "========================================\n";
    return 0;
}
