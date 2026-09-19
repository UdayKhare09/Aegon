#include "http/Server.h"
#include "http/middleware/Cors.h"
#include "http/v1/Http1Parser.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>

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

// 1. Permissive CORS on standard GET request
void test_1_permissive_get() {
    std::cout << "[TEST 1] Testing permissive CORS on GET...\n";
    Router router;
    bool handler_ran = false;

    router.use(cors(CorsConfig::permissive()));
    router.get("/data", [&handler_ran](Context& ctx) {
        handler_ran = true;
        ctx.res().status(StatusCode::Ok).body("hello");
    });

    Response res = dispatch_offline(router,
        "GET /data HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Origin: https://frontend.example.com\r\n\r\n");

    assert(handler_ran);
    assert(res.status() == StatusCode::Ok);
    assert(res.body() == "hello");
    assert(res.headers().get("access-control-allow-origin") == "*");
    std::cout << "  -> PASS\n";
}

// 2. Preflight OPTIONS request with early short-circuit
void test_2_preflight_options_short_circuit() {
    std::cout << "[TEST 2] Testing preflight OPTIONS short-circuit...\n";
    Router router;
    bool handler_ran = false;

    router.use(cors(CorsConfig::permissive()));
    router.post("/items", [&handler_ran](Context& ctx) {
        handler_ran = true;
        ctx.res().status(StatusCode::Created).body("item created");
    });

    Response res = dispatch_offline(router,
        "OPTIONS /items HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Origin: https://app.example.com\r\n"
        "Access-Control-Request-Method: POST\r\n"
        "Access-Control-Request-Headers: Authorization, Content-Type\r\n\r\n");

    assert(!handler_ran); // Short-circuit: backend handler MUST NOT run
    assert(res.status() == StatusCode::NoContent);
    assert(res.headers().get("access-control-allow-origin") == "*");
    assert(res.headers().contains("access-control-allow-methods"));
    assert(res.headers().get("access-control-allow-methods")->find("POST") != std::string_view::npos);
    assert(res.headers().get("access-control-max-age") == "86400");
    std::cout << "  -> PASS\n";
}

// 3. Strict origins with credentials (must reflect origin, not *)
void test_3_strict_credentials() {
    std::cout << "[TEST 3] Testing strict CORS with credentials reflection...\n";
    Router router;

    router.use(cors(CorsConfig{
        .allowed_origins = {"https://secure.app.com"},
        .allow_credentials = true
    }));

    router.get("/profile", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("user profile");
    });

    Response res = dispatch_offline(router,
        "GET /profile HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Origin: https://secure.app.com\r\n\r\n");

    assert(res.status() == StatusCode::Ok);
    assert(res.headers().get("access-control-allow-origin") == "https://secure.app.com");
    assert(res.headers().get("access-control-allow-credentials") == "true");
    assert(res.headers().contains("vary"));
    assert(res.headers().get("vary")->find("Origin") != std::string_view::npos);
    std::cout << "  -> PASS\n";
}

// 4. Disallowed origin rejected
void test_4_disallowed_origin() {
    std::cout << "[TEST 4] Testing disallowed origin rejection...\n";
    Router router;

    router.use(cors(CorsConfig{
        .allowed_origins = {"https://trusted.com"}
    }));

    router.get("/secret", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("secret");
    });

    // Actual request from untrusted origin: no Access-Control-Allow-Origin emitted
    {
        Response res = dispatch_offline(router,
            "GET /secret HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: https://evil.com\r\n\r\n");

        assert(res.status() == StatusCode::Ok);
        assert(!res.headers().contains("access-control-allow-origin"));
    }

    // Preflight request from untrusted origin: 204 with no Access-Control-Allow-Origin
    {
        Response res = dispatch_offline(router,
            "OPTIONS /secret HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: https://evil.com\r\n"
            "Access-Control-Request-Method: GET\r\n\r\n");

        assert(res.status() == StatusCode::NoContent);
        assert(!res.headers().contains("access-control-allow-origin"));
    }
    std::cout << "  -> PASS\n";
}

// 5. Spring Boot-style subdomain glob pattern matching (*.myapp.com)
void test_5_subdomain_patterns() {
    std::cout << "[TEST 5] Testing Spring Boot subdomain wildcard patterns...\n";
    Router router;

    router.use(cors(CorsConfig{
        .origin_patterns = {"https://*.myapp.com", "https://dev-*.partner.io"}
    }));

    router.get("/api/status", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("ok");
    });

    // Subdomain 1
    {
        Response res = dispatch_offline(router,
            "GET /api/status HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: https://dashboard.myapp.com\r\n\r\n");
        assert(res.headers().get("access-control-allow-origin") == "https://dashboard.myapp.com");
    }

    // Subdomain 2
    {
        Response res = dispatch_offline(router,
            "GET /api/status HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: https://admin.myapp.com\r\n\r\n");
        assert(res.headers().get("access-control-allow-origin") == "https://admin.myapp.com");
    }

    // Prefix wildcard
    {
        Response res = dispatch_offline(router,
            "GET /api/status HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: https://dev-tenant8.partner.io\r\n\r\n");
        assert(res.headers().get("access-control-allow-origin") == "https://dev-tenant8.partner.io");
    }

    // Suffix attack (e.g. myapp.com.evil.com) -> must be rejected
    {
        Response res = dispatch_offline(router,
            "GET /api/status HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: https://myapp.com.evil.com\r\n\r\n");
        assert(!res.headers().contains("access-control-allow-origin"));
    }
    std::cout << "  -> PASS\n";
}

// 6. Port wildcard pattern (http://localhost:*)
void test_6_port_patterns() {
    std::cout << "[TEST 6] Testing localhost port wildcard patterns...\n";
    Router router;

    router.use(cors(CorsConfig{
        .origin_patterns = {"http://localhost:*"}
    }));

    router.get("/dev", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("dev");
    });

    // Port 3000
    {
        Response res = dispatch_offline(router,
            "GET /dev HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: http://localhost:3000\r\n\r\n");
        assert(res.headers().get("access-control-allow-origin") == "http://localhost:3000");
    }

    // Port 8080
    {
        Response res = dispatch_offline(router,
            "GET /dev HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: http://localhost:8080\r\n\r\n");
        assert(res.headers().get("access-control-allow-origin") == "http://localhost:8080");
    }

    // Non-localhost with port -> rejected
    {
        Response res = dispatch_offline(router,
            "GET /dev HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: http://attacker:3000\r\n\r\n");
        assert(!res.headers().contains("access-control-allow-origin"));
    }
    std::cout << "  -> PASS\n";
}

// 7. Dynamic tenant lambda origin validator
void test_7_dynamic_lambda_validator() {
    std::cout << "[TEST 7] Testing dynamic runtime origin validator lambda...\n";
    Router router;

    std::vector<std::string> active_tenants = {"alpha-corp", "beta-inc"};

    router.use(cors(CorsConfig{
        .origin_validator = [&active_tenants](std::string_view origin) {
            for (const auto& t : active_tenants) {
                if (origin.find(t) != std::string_view::npos) {
                    return true;
                }
            }
            return false;
        }
    }));

    router.get("/tenant/data", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("tenant data");
    });

    // Valid tenant alpha
    {
        Response res = dispatch_offline(router,
            "GET /tenant/data HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: https://alpha-corp.external-cloud.com\r\n\r\n");
        assert(res.headers().get("access-control-allow-origin") == "https://alpha-corp.external-cloud.com");
    }

    // Unknown tenant gamma -> rejected
    {
        Response res = dispatch_offline(router,
            "GET /tenant/data HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: https://gamma-co.external-cloud.com\r\n\r\n");
        assert(!res.headers().contains("access-control-allow-origin"));
    }
    std::cout << "  -> PASS\n";
}

// 8. W3C Private Network Access (PNA)
void test_8_private_network_access() {
    std::cout << "[TEST 8] Testing Private Network Access (PNA)...\n";
    Router router;

    router.use(cors(CorsConfig{
        .allowed_origins = {"*"},
        .allow_private_network = true
    }));

    router.get("/local-service", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("private");
    });

    Response res = dispatch_offline(router,
        "OPTIONS /local-service HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Origin: https://public-cloud.com\r\n"
        "Access-Control-Request-Method: GET\r\n"
        "Access-Control-Request-Private-Network: true\r\n\r\n");

    assert(res.status() == StatusCode::NoContent);
    assert(res.headers().get("access-control-allow-private-network") == "true");
    std::cout << "  -> PASS\n";
}

// 9. Custom exposed headers and max-age
void test_9_custom_headers_and_max_age() {
    std::cout << "[TEST 9] Testing custom exposed headers and max-age...\n";
    using namespace std::chrono_literals;
    Router router;

    router.use(cors(CorsConfig{
        .allowed_origins = {"*"},
        .allowed_headers = {"Authorization", "X-Trace-ID", "Content-Type"},
        .expose_headers = {"X-Request-ID", "X-Total-Count"},
        .max_age = 7200s
    }));

    router.get("/items", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("items");
    });

    // Actual request
    {
        Response res = dispatch_offline(router,
            "GET /items HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: https://frontend.com\r\n\r\n");
        assert(res.headers().get("access-control-expose-headers") == "X-Request-ID, X-Total-Count");
    }

    // Preflight request
    {
        Response res = dispatch_offline(router,
            "OPTIONS /items HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Origin: https://frontend.com\r\n"
            "Access-Control-Request-Method: GET\r\n\r\n");
        assert(res.headers().get("access-control-max-age") == "7200");
        assert(res.headers().get("access-control-allow-headers") == "Authorization, X-Trace-ID, Content-Type");
    }
    std::cout << "  -> PASS\n";
}

// 10. Strict preset helper verification
void test_10_strict_preset() {
    std::cout << "[TEST 10] Testing CorsConfig::strict() preset...\n";
    Router router;

    router.use(cors(CorsConfig::strict({"https://portal.company.com"})));
    router.get("/api", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("portal");
    });

    Response res = dispatch_offline(router,
        "GET /api HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Origin: https://portal.company.com\r\n\r\n");

    assert(res.status() == StatusCode::Ok);
    assert(res.headers().get("access-control-allow-origin") == "https://portal.company.com");
    assert(res.headers().get("access-control-allow-credentials") == "true");
    assert(res.headers().get("access-control-expose-headers") == "X-Request-ID, Content-Length");
    std::cout << "  -> PASS\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "      RUNNING AEGON CORS TEST SUITE     \n";
    std::cout << "========================================\n";

    test_1_permissive_get();
    test_2_preflight_options_short_circuit();
    test_3_strict_credentials();
    test_4_disallowed_origin();
    test_5_subdomain_patterns();
    test_6_port_patterns();
    test_7_dynamic_lambda_validator();
    test_8_private_network_access();
    test_9_custom_headers_and_max_age();
    test_10_strict_preset();

    std::cout << "========================================\n";
    std::cout << "     ALL 10 CORS TESTS PASSED!          \n";
    std::cout << "========================================\n";
    return 0;
}
