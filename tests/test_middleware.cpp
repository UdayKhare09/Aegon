#include "http/Server.h"
#include "http/Middleware.h"
#include "http/v1/Http1Parser.h"
#include "http/ProblemDetails.h"
#include <iostream>
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aegon;
using namespace aegon::http;

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

// 1. Global middleware runs before handler (sync + async overloads)
void test_1_global_middleware_sync_async() {
    std::cout << "[TEST 1] Testing global middleware (sync + async)...\n";
    Server server;
    server.use([](Context& ctx, Next next) -> core::Task<void> {
        ctx.res().header("x-async-global", "true");
        co_await next(ctx);
    });
    server.use([](Context& ctx) {
        ctx.res().header("x-sync-global", "true");
    });

    server.router().get("/hello", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("Hello World");
    });

    Response res = dispatch_offline(server.router(), "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert(res.status() == StatusCode::Ok);
    assert(res.body() == "Hello World");
    assert(res.headers().get("x-async-global") == "true");
    assert(res.headers().get("x-sync-global") == "true");
    std::cout << "  -> PASS\n";
}

// 2. Short-circuit: middleware does NOT call next() — handler never runs
void test_2_short_circuit() {
    std::cout << "[TEST 2] Testing short-circuit execution...\n";
    Router router;
    bool handler_ran = false;

    router.use([&handler_ran](Context& ctx, Next next) -> core::Task<void> {
        auto auth = ctx.req().header("authorization");
        if (auth != "Bearer secret_token") {
            ctx.res().status(StatusCode::Unauthorized).body("Unauthorized");
            co_return; // Do NOT call next
        }
        co_await next(ctx);
    });

    router.get("/protected", [&handler_ran](Context& ctx) {
        handler_ran = true;
        ctx.res().status(StatusCode::Ok).body("Access Granted");
    });

    // Request without token -> short-circuit
    {
        Response res = dispatch_offline(router, "GET /protected HTTP/1.1\r\nHost: localhost\r\n\r\n");
        assert(res.status() == StatusCode::Unauthorized);
        assert(res.body() == "Unauthorized");
        assert(!handler_ran);
    }

    // Request with valid token -> passes through
    {
        Response res = dispatch_offline(router, "GET /protected HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer secret_token\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(res.body() == "Access Granted");
        assert(handler_ran);
    }
    std::cout << "  -> PASS\n";
}

// 3. Onion post-handler: code after co_await next(ctx) runs after handler
void test_3_onion_post_handler() {
    std::cout << "[TEST 3] Testing onion post-handler execution...\n";
    Router router;
    std::vector<std::string> steps;

    router.use([&steps](Context& ctx, Next next) -> core::Task<void> {
        steps.push_back("pre");
        co_await next(ctx);
        steps.push_back("post");
        ctx.res().header("x-post-modified", "1");
    });

    router.get("/onion", [&steps](Context& ctx) {
        steps.push_back("handler");
        ctx.res().status(StatusCode::Ok).body("done");
    });

    Response res = dispatch_offline(router, "GET /onion HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert(res.status() == StatusCode::Ok);
    assert(res.body() == "done");
    assert(res.headers().get("x-post-modified") == "1");

    assert(steps.size() == 3);
    assert(steps[0] == "pre");
    assert(steps[1] == "handler");
    assert(steps[2] == "post");
    std::cout << "  -> PASS\n";
}

// 4. RouteGroup middleware applies only to group routes, not global ones
void test_4_route_group_middleware_scope() {
    std::cout << "[TEST 4] Testing RouteGroup middleware scope isolation...\n";
    Router router;

    auto api = router.group("/api");
    api.use([](Context& ctx, Next next) -> core::Task<void> {
        ctx.res().header("x-group", "api");
        co_await next(ctx);
    });

    api.get("/users", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("users");
    });

    router.get("/public", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("public");
    });

    // Public route should not have x-group header
    {
        Response res = dispatch_offline(router, "GET /public HTTP/1.1\r\nHost: localhost\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(!res.headers().contains("x-group"));
    }

    // API route must have x-group header
    {
        Response res = dispatch_offline(router, "GET /api/users HTTP/1.1\r\nHost: localhost\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(res.headers().get("x-group") == "api");
    }
    std::cout << "  -> PASS\n";
}

// 5. Sub-group inherits parent group middleware, adds its own
void test_5_group_inheritance() {
    std::cout << "[TEST 5] Testing RouteGroup inheritance...\n";
    Router router;
    std::vector<std::string> steps;

    auto api = router.group("/api");
    api.use([&steps](Context& ctx, Next next) -> core::Task<void> {
        steps.push_back("parent");
        co_await next(ctx);
    });

    auto v1 = api.group("/v1");
    v1.use([&steps](Context& ctx, Next next) -> core::Task<void> {
        steps.push_back("child");
        co_await next(ctx);
    });

    v1.get("/resource", [&steps](Context& ctx) {
        steps.push_back("resource");
        ctx.res().status(StatusCode::Ok).body("resource");
    });

    api.get("/ping", [&steps](Context& ctx) {
        steps.push_back("ping");
        ctx.res().status(StatusCode::Ok).body("pong");
    });

    // Hit sub-group route
    steps.clear();
    Response res1 = dispatch_offline(router, "GET /api/v1/resource HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert(res1.status() == StatusCode::Ok);
    assert(steps.size() == 3);
    assert(steps[0] == "parent");
    assert(steps[1] == "child");
    assert(steps[2] == "resource");

    // Hit parent group route: child middleware should NOT run
    steps.clear();
    Response res2 = dispatch_offline(router, "GET /api/ping HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert(res2.status() == StatusCode::Ok);
    assert(steps.size() == 2);
    assert(steps[0] == "parent");
    assert(steps[1] == "ping");

    std::cout << "  -> PASS\n";
}

// 6. Per-route middleware runs between group mw and handler
void test_6_per_route_middleware() {
    std::cout << "[TEST 6] Testing per-route middleware...\n";
    Router router;
    std::vector<std::string> steps;

    auto api = router.group("/api");
    api.use([&steps](Context& ctx, Next next) -> core::Task<void> {
        steps.push_back("group");
        co_await next(ctx);
    });

    std::vector<MiddlewareFn> route_mw;
    route_mw.push_back([&steps](Context& ctx, Next next) -> core::Task<void> {
        steps.push_back("route");
        co_await next(ctx);
    });

    api.get("/special", std::move(route_mw), [&steps](Context& ctx) {
        steps.push_back("handler");
        ctx.res().status(StatusCode::Ok).body("special");
    });

    Response res = dispatch_offline(router, "GET /api/special HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert(res.status() == StatusCode::Ok);
    assert(steps.size() == 3);
    assert(steps[0] == "group");
    assert(steps[1] == "route");
    assert(steps[2] == "handler");
    std::cout << "  -> PASS\n";
}

// 7. Per-request store: ctx.set<T>() in middleware, ctx.get<T>() in handler
struct UserClaims {
    std::string user_id;
    int role{0};
};

void test_7_per_request_store_set_get() {
    std::cout << "[TEST 7] Testing Context per-request typed data bag (set, get, has)...\n";
    Router router;

    router.use([](Context& ctx, Next next) -> core::Task<void> {
        ctx.set<UserClaims>(UserClaims{.user_id = "usr_9981", .role = 2});
        co_await next(ctx);
    });

    router.get("/profile", [](Context& ctx) {
        assert(ctx.has<UserClaims>());
        UserClaims* claims = ctx.get<UserClaims>();
        assert(claims != nullptr);
        assert(claims->user_id == "usr_9981");
        assert(claims->role == 2);
        ctx.res().status(StatusCode::Ok).body("uid=" + claims->user_id);
    });

    Response res = dispatch_offline(router, "GET /profile HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert(res.status() == StatusCode::Ok);
    assert(res.body() == "uid=usr_9981");
    std::cout << "  -> PASS\n";
}

// 8. ctx.local<T>() throws if key was never set
void test_8_local_throws_if_missing() {
    std::cout << "[TEST 8] Testing Context::local<T>() throws when missing...\n";
    Router router;

    router.get("/missing", [](Context& ctx) {
        assert(!ctx.has<UserClaims>());
        assert(ctx.get<UserClaims>() == nullptr);
        bool threw = false;
        try {
            [[maybe_unused]] auto& c = ctx.local<UserClaims>();
        } catch (const std::runtime_error&) {
            threw = true;
        }
        assert(threw);
        ctx.res().status(StatusCode::Ok).body("caught");
    });

    Response res = dispatch_offline(router, "GET /missing HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert(res.status() == StatusCode::Ok);
    assert(res.body() == "caught");
    std::cout << "  -> PASS\n";
}

// 9. Full stack: global → group → per-route → handler → post-hook
void test_9_full_stack_onion() {
    std::cout << "[TEST 9] Testing full stack onion pipeline...\n";
    Router router;
    std::vector<std::string> trace;

    router.use([&trace](Context& ctx, Next next) -> core::Task<void> {
        trace.push_back("global_in");
        co_await next(ctx);
        trace.push_back("global_out");
    });

    auto grp = router.group("/admin");
    grp.use([&trace](Context& ctx, Next next) -> core::Task<void> {
        trace.push_back("group_in");
        co_await next(ctx);
        trace.push_back("group_out");
    });

    std::vector<MiddlewareFn> route_mw;
    route_mw.push_back([&trace](Context& ctx, Next next) -> core::Task<void> {
        trace.push_back("route_in");
        co_await next(ctx);
        trace.push_back("route_out");
    });

    grp.get("/dashboard", std::move(route_mw), [&trace](Context& ctx) {
        trace.push_back("handler");
        ctx.res().status(StatusCode::Ok).body("dashboard");
    });

    Response res = dispatch_offline(router, "GET /admin/dashboard HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert(res.status() == StatusCode::Ok);
    assert(res.body() == "dashboard");

    std::vector<std::string> expected = {
        "global_in",
        "group_in",
        "route_in",
        "handler",
        "route_out",
        "group_out",
        "global_out"
    };
    assert(trace == expected);
    std::cout << "  -> PASS\n";
}

// 10. Error in middleware propagates to error_handler_
void test_10_error_in_middleware() {
    std::cout << "[TEST 10] Testing exception propagation from middleware to error_handler_...\n";
    Router router;

    router.use([](Context&, Next) -> core::Task<void> {
        throw std::invalid_argument("Invalid token in middleware");
        co_return;
    });

    router.get("/error_test", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("should_not_run");
    });

    bool error_handler_called = false;
    router.set_error_handler([&error_handler_called](Context& ctx, std::exception_ptr ex) -> core::Task<void> {
        error_handler_called = true;
        std::string msg;
        try {
            if (ex) std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            msg = e.what();
        }
        ctx.res().status(StatusCode::BadRequest).body("caught: " + msg);
        co_return;
    });

    Response res = dispatch_offline(router, "GET /error_test HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert(error_handler_called);
    assert(res.status() == StatusCode::BadRequest);
    assert(res.body() == "caught: Invalid token in middleware");
    std::cout << "  -> PASS\n";
}

// 11. Multiple global middleware execute in registration order
void test_11_multiple_global_registration_order() {
    std::cout << "[TEST 11] Testing multiple global middleware registration order...\n";
    Router router;
    std::vector<int> order;

    router.use([&order](Context& ctx, Next next) -> core::Task<void> {
        order.push_back(1);
        co_await next(ctx);
    });
    router.use([&order](Context& ctx, Next next) -> core::Task<void> {
        order.push_back(2);
        co_await next(ctx);
    });
    router.use([&order](Context& ctx, Next next) -> core::Task<void> {
        order.push_back(3);
        co_await next(ctx);
    });

    router.get("/seq", [&order](Context& ctx) {
        order.push_back(4);
        ctx.res().status(StatusCode::Ok).body("seq");
    });

    Response res = dispatch_offline(router, "GET /seq HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert(res.status() == StatusCode::Ok);
    std::vector<int> expected = {1, 2, 3, 4};
    assert(order == expected);
    std::cout << "  -> PASS\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   RUNNING AEGON MIDDLEWARE TEST SUITE  \n";
    std::cout << "========================================\n";

    test_1_global_middleware_sync_async();
    test_2_short_circuit();
    test_3_onion_post_handler();
    test_4_route_group_middleware_scope();
    test_5_group_inheritance();
    test_6_per_route_middleware();
    test_7_per_request_store_set_get();
    test_8_local_throws_if_missing();
    test_9_full_stack_onion();
    test_10_error_in_middleware();
    test_11_multiple_global_registration_order();

    std::cout << "========================================\n";
    std::cout << "   ALL 11 MIDDLEWARE TESTS PASSED!      \n";
    std::cout << "========================================\n";
    return 0;
}
