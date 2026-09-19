#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <optional>

#include "http/Server.h"
#include "http/Middleware.h"
#include "http/v1/Http1Parser.h"
#include "http/middleware/Auth.h"
#include "http/jwt/JwtAlgorithm.h"

using namespace aegon;
using namespace aegon::http;
using namespace aegon::http::middleware;

struct UserPrincipal {
    uint64_t id;
    std::string username;
};

struct SessionPrincipal {
    std::string session_id;
    uint64_t user_id;
};

struct ApiKeyPrincipal {
    std::string key;
    std::string tier;
};

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

void test_bearer_auth() {
    std::cout << "[TEST] bearer_auth middleware..." << std::endl;
    Router router;

    router.use(bearer_auth<UserPrincipal>({
        .validator = [](std::string_view token, Context&) -> core::Task<std::optional<UserPrincipal>> {
            if (token == "valid_token_123") {
                co_return UserPrincipal{1, "alice"};
            }
            co_return std::nullopt;
        }
    }));

    router.get("/profile", [](Context& ctx) {
        const auto* user = ctx.get<UserPrincipal>();
        assert(user != nullptr);
        ctx.res().status(StatusCode::Ok).body("Hello " + user->username);
    });

    // 1. Success with Bearer token
    {
        Response res = dispatch_offline(router, "GET /profile HTTP/1.1\r\nAuthorization: Bearer valid_token_123\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(res.body() == "Hello alice");
    }

    // 2. Missing header -> 401 Problem Details
    {
        Response res = dispatch_offline(router, "GET /profile HTTP/1.1\r\n\r\n");
        assert(res.status() == StatusCode::Unauthorized);
        assert(res.headers().get("www-authenticate") == "Bearer");
        assert(res.headers().get("content-type") == "application/problem+json");
        assert(res.body().find("\"status\":401") != std::string::npos);
    }

    // 3. Invalid token -> 401 Problem Details
    {
        Response res = dispatch_offline(router, "GET /profile HTTP/1.1\r\nAuthorization: Bearer bad_token\r\n\r\n");
        assert(res.status() == StatusCode::Unauthorized);
    }

    // 4. Scheme mismatch (not Bearer) -> 401 Problem Details
    {
        Response res = dispatch_offline(router, "GET /profile HTTP/1.1\r\nAuthorization: Token 12345\r\n\r\n");
        assert(res.status() == StatusCode::Unauthorized);
    }

    // 5. Custom on_unauthorized
    {
        Router custom_router;
        custom_router.use(bearer_auth<UserPrincipal>({
            .validator = [](std::string_view, Context&) -> core::Task<std::optional<UserPrincipal>> {
                co_return std::nullopt;
            },
            .on_unauthorized = [](Context& ctx) {
                ctx.res().status(StatusCode::Unauthorized).header("X-Custom-Auth", "Denied").body("custom-401");
            }
        }));
        custom_router.get("/test", [](Context& ctx) { ctx.res().status(StatusCode::Ok); });

        Response res = dispatch_offline(custom_router, "GET /test HTTP/1.1\r\nAuthorization: Bearer any\r\n\r\n");
        assert(res.status() == StatusCode::Unauthorized);
        assert(res.headers().get("x-custom-auth") == "Denied");
        assert(res.body() == "custom-401");
    }

    // 6. abort_on_failure = false (optional auth)
    {
        Router optional_router;
        optional_router.use(bearer_auth<UserPrincipal>({
            .validator = [](std::string_view token, Context&) -> core::Task<std::optional<UserPrincipal>> {
                if (token == "alice_tok") co_return UserPrincipal{1, "alice"};
                co_return std::nullopt;
            },
            .abort_on_failure = false
        }));

        optional_router.get("/feed", [](Context& ctx) {
            if (ctx.has<UserPrincipal>()) {
                ctx.res().status(StatusCode::Ok).body("authenticated feed");
            } else {
                ctx.res().status(StatusCode::Ok).body("guest feed");
            }
        });

        Response guest_res = dispatch_offline(optional_router, "GET /feed HTTP/1.1\r\n\r\n");
        assert(guest_res.status() == StatusCode::Ok);
        assert(guest_res.body() == "guest feed");

        Response auth_res = dispatch_offline(optional_router, "GET /feed HTTP/1.1\r\nAuthorization: Bearer alice_tok\r\n\r\n");
        assert(auth_res.status() == StatusCode::Ok);
        assert(auth_res.body() == "authenticated feed");
    }

    std::cout << "  -> PASS\n";
}

void test_cookie_auth() {
    std::cout << "[TEST] cookie_auth middleware..." << std::endl;
    Router router;

    router.use(cookie_auth<SessionPrincipal>("session_id", {
        .validator = [](std::string_view sid, Context&) -> core::Task<std::optional<SessionPrincipal>> {
            if (sid == "sess_abc123") {
                co_return SessionPrincipal{"sess_abc123", 42};
            }
            co_return std::nullopt;
        }
    }));

    router.get("/dashboard", [](Context& ctx) {
        const auto* sess = ctx.get<SessionPrincipal>();
        assert(sess != nullptr);
        ctx.res().status(StatusCode::Ok).body("User " + std::to_string(sess->user_id));
    });

    // 1. Success with valid cookie
    {
        Response res = dispatch_offline(router, "GET /dashboard HTTP/1.1\r\nCookie: session_id=sess_abc123\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(res.body() == "User 42");
    }

    // 2. Missing cookie -> 401 Problem Details
    {
        Response res = dispatch_offline(router, "GET /dashboard HTTP/1.1\r\n\r\n");
        assert(res.status() == StatusCode::Unauthorized);
        assert(res.headers().get("content-type") == "application/problem+json");
    }

    // 3. Wrong cookie name -> 401
    {
        Response res = dispatch_offline(router, "GET /dashboard HTTP/1.1\r\nCookie: other_cookie=xyz\r\n\r\n");
        assert(res.status() == StatusCode::Unauthorized);
    }

    // 4. Invalid session value -> 401
    {
        Response res = dispatch_offline(router, "GET /dashboard HTTP/1.1\r\nCookie: session_id=unknown\r\n\r\n");
        assert(res.status() == StatusCode::Unauthorized);
    }

    std::cout << "  -> PASS\n";
}

void test_api_key_auth() {
    std::cout << "[TEST] api_key_auth middleware..." << std::endl;
    Router router;

    router.use(api_key_auth<ApiKeyPrincipal>("x-api-key", {
        .validator = [](std::string_view key, Context&) -> core::Task<std::optional<ApiKeyPrincipal>> {
            if (key == "ak_live_enterprise") {
                co_return ApiKeyPrincipal{"ak_live_enterprise", "enterprise"};
            }
            co_return std::nullopt;
        }
    }));

    router.get("/v1/data", [](Context& ctx) {
        const auto* key = ctx.get<ApiKeyPrincipal>();
        assert(key != nullptr);
        ctx.res().status(StatusCode::Ok).body("Tier: " + key->tier);
    });

    // 1. Success with API Key
    {
        Response res = dispatch_offline(router, "GET /v1/data HTTP/1.1\r\nX-API-Key: ak_live_enterprise\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(res.body() == "Tier: enterprise");
    }

    // 2. Missing header -> 401 Problem Details
    {
        Response res = dispatch_offline(router, "GET /v1/data HTTP/1.1\r\n\r\n");
        assert(res.status() == StatusCode::Unauthorized);
        assert(res.headers().get("content-type") == "application/problem+json");
    }

    // 3. Invalid key -> 401
    {
        Response res = dispatch_offline(router, "GET /v1/data HTTP/1.1\r\nX-API-Key: invalid_key\r\n\r\n");
        assert(res.status() == StatusCode::Unauthorized);
    }

    std::cout << "  -> PASS\n";
}

void test_basic_auth() {
    std::cout << "[TEST] basic_auth middleware..." << std::endl;
    Router router;

    router.use(basic_auth<UserPrincipal>({
        .validator = [](std::string_view user, std::string_view pass, Context&) -> core::Task<std::optional<UserPrincipal>> {
            if (user == "admin" && pass == "secret123") {
                co_return UserPrincipal{10, "admin"};
            }
            co_return std::nullopt;
        },
        .realm = "AdminArea"
    }));

    router.get("/metrics", [](Context& ctx) {
        const auto* u = ctx.get<UserPrincipal>();
        assert(u != nullptr);
        ctx.res().status(StatusCode::Ok).body("Metrics for " + u->username);
    });

    // Encode admin:secret123 in base64 -> YWRtaW46c2VjcmV0MTIz
    // 1. Success with Basic auth
    {
        Response res = dispatch_offline(router, "GET /metrics HTTP/1.1\r\nAuthorization: Basic YWRtaW46c2VjcmV0MTIz\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(res.body() == "Metrics for admin");
    }

    // 2. Missing header -> 401 with WWW-Authenticate: Basic realm="AdminArea"
    {
        Response res = dispatch_offline(router, "GET /metrics HTTP/1.1\r\n\r\n");
        assert(res.status() == StatusCode::Unauthorized);
        assert(res.headers().get("www-authenticate") == "Basic realm=\"AdminArea\"");
    }

    // 3. Invalid credentials -> 401
    {
        // admin:wrongpass -> YWRtaW46d3JvbmdwYXNz
        Response res = dispatch_offline(router, "GET /metrics HTTP/1.1\r\nAuthorization: Basic YWRtaW46d3JvbmdwYXNz\r\n\r\n");
        assert(res.status() == StatusCode::Unauthorized);
    }

    // 4. Invalid base64 -> 401
    {
        Response res = dispatch_offline(router, "GET /metrics HTTP/1.1\r\nAuthorization: Basic @@@notbase64@@@\r\n\r\n");
        assert(res.status() == StatusCode::Unauthorized);
    }

    std::cout << "  -> PASS\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "    Aegon Auth Middleware Tests\n";
    std::cout << "========================================\n";

    test_bearer_auth();
    test_cookie_auth();
    test_api_key_auth();
    test_basic_auth();

    std::cout << "\nALL AUTH MIDDLEWARE TESTS PASSED!\n";
    return 0;
}
