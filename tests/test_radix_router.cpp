#include "http/Router.h"
#include "http/Request.h"
#include "http/Response.h"
#include "http/Context.h"
#include "data/uuid/UUIDGenerator.h"
#include <iostream>
#include <cassert>
#include <string>

#define TEST_CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace aegon;
using namespace aegon::http;

inline void invoke_handler(const Handler& handler, Context& ctx) {
    auto task = handler(ctx);
    task.resume();
}

void test_radix_tree_static_routes() {
    std::cout << "[Test 1] Testing Radix Tree static route matching & prefix compression...\n";

    Router router;
    bool root_called = false;
    bool health_called = false;
    bool team_called = false;
    bool test_called = false;

    // Concise synchronous handlers: NO '-> core::Task<void>' and NO 'co_return;'
    router.get("/", [&](Context& ctx) {
        root_called = true;
        ctx.res().status(StatusCode::Ok).text("root");
    });

    router.get("/health", [&](Context& ctx) {
        health_called = true;
        ctx.res().status(StatusCode::Ok).text("health");
    });

    // Test prefix splitting: 'te' is shared between 'team' and 'test'
    router.get("/team", [&](Context& ctx) {
        team_called = true;
        ctx.res().status(StatusCode::Ok).text("team");
    });

    router.get("/test", [&](Context& ctx) {
        test_called = true;
        ctx.res().status(StatusCode::Ok).text("test");
    });

    // Match "/"
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        TEST_CHECK(!res.method_not_allowed);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
        TEST_CHECK(root_called);
    }

    // Match "/health"
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/health");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
        TEST_CHECK(health_called);
    }

    // Match "/team"
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/team");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
        TEST_CHECK(team_called);
    }

    // Match "/test"
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/test");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
        TEST_CHECK(test_called);
    }

    // Non-existent route
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/unknown");
        auto res = router.match(req);
        TEST_CHECK(!res.route_found);
    }

    std::cout << "  -> PASS: Static routes and prefix compression verified.\n";
}

void test_radix_tree_parameters() {
    std::cout << "[Test 2] Testing Radix Tree parameter extraction & SIMD UUID...\n";

    Router router;

    router.get("/users/:id", [](Context& ctx) {
        auto id_str = ctx.param("id");
        TEST_CHECK(id_str.has_value());
        ctx.res().status(StatusCode::Ok).text(std::string(*id_str));
    });

    router.get("/users/:user_id/posts/:post_id", [](Context& ctx) {
        auto uid = ctx.param("user_id");
        auto pid = ctx.param("post_id");
        TEST_CHECK(uid == "42");
        TEST_CHECK(pid == "999");
        ctx.res().status(StatusCode::Ok).text("ok");
    });

    // Match single param
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/users/12345");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        TEST_CHECK(req.param("id") == "12345");
    }

    // Match SIMD UUID in path
    {
        auto test_uuid = data::UUIDGenerator::v7();
        std::string uuid_str = test_uuid.to_string();
        std::string full_path = "/users/" + uuid_str;

        Request req;
        req.set_method(Method::GET);
        req.set_path(full_path);
        auto res = router.match(req);
        TEST_CHECK(res.route_found);

        Response resp;
        Context ctx(req, resp);
        auto extracted_uuid = ctx.param_uuid("id");
        TEST_CHECK(extracted_uuid.has_value());
        TEST_CHECK(*extracted_uuid == test_uuid);
    }

    // Match multi params
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/users/42/posts/999");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
    }

    std::cout << "  -> PASS: Parameter extraction and multi-param routing verified.\n";
}

void test_radix_tree_wildcards() {
    std::cout << "[Test 3] Testing Radix Tree wildcard (*filepath) matching...\n";

    Router router;

    router.get("/static/*filepath", [](Context& ctx) {
        auto path = ctx.param("filepath");
        TEST_CHECK(path.has_value());
        ctx.res().status(StatusCode::Ok).text(std::string(*path));
    });

    Request req;
    req.set_method(Method::GET);
    req.set_path("/static/css/themes/dark.css");
    auto res = router.match(req);
    TEST_CHECK(res.route_found);
    TEST_CHECK(req.param("filepath") == "css/themes/dark.css");

    std::cout << "  -> PASS: Wildcard route matching verified.\n";
}

void test_route_groups() {
    std::cout << "[Test 4] Testing RouteGroup hierarchical grouping...\n";

    Router router;

    // Create top-level API group
    auto api = router.group("/api");

    // Create nested v1 sub-group
    auto v1 = api.group("/v1");
    v1.get("/users", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).text("v1_users");
    });
    v1.post("/users", [](Context& ctx) {
        ctx.res().status(StatusCode::Created).text("v1_create_user");
    });
    v1.get("/users/:id", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).text("v1_get_user");
    });

    // Create nested v2 sub-group
    auto v2 = api.group("/v2");
    v2.get("/users", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).text("v2_users");
    });

    // Test GET /api/v1/users
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/api/v1/users");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
        TEST_CHECK(resp.body() == "v1_users");
    }

    // Test POST /api/v1/users
    {
        Request req;
        req.set_method(Method::POST);
        req.set_path("/api/v1/users");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
        TEST_CHECK(resp.body() == "v1_create_user");
    }

    // Test GET /api/v1/users/42
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/api/v1/users/42");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        TEST_CHECK(req.param("id") == "42");
    }

    // Test GET /api/v2/users
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/api/v2/users");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
        TEST_CHECK(resp.body() == "v2_users");
    }

    std::cout << "  -> PASS: Hierarchical RouteGroups (/api/v1 and /api/v2) verified.\n";
}

void test_method_not_allowed() {
    std::cout << "[Test 5] Testing Method Not Allowed (405)...\n";

    Router router;
    router.post("/items", [](Context& ctx) {
        ctx.res().status(StatusCode::Created).text("created");
    });

    Request req;
    req.set_method(Method::GET);
    req.set_path("/items");
    auto res = router.match(req);
    TEST_CHECK(res.route_found);
    TEST_CHECK(res.method_not_allowed);
    TEST_CHECK(res.handler == nullptr);

    std::cout << "  -> PASS: Method Not Allowed correctly detected.\n";
}

void test_async_and_sync_handlers() {
    std::cout << "[Test 6] Testing Async Coroutine Task & Concise Sync Handlers...\n";

    Router router;

    // Synchronous handler (plain void return, no coroutine)
    router.get("/sync", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).text("sync_response");
    });

    // Asynchronous coroutine Task<void> handler
    router.get("/async", [](Context& ctx) -> core::Task<void> {
        ctx.res().status(StatusCode::Ok).text("async_response");
        co_return;
    });

    // Test sync
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/sync");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
        TEST_CHECK(resp.body() == "sync_response");
    }

    // Test async
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/async");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        auto task = (*res.handler)(ctx);
        task.resume();
        TEST_CHECK(resp.body() == "async_response");
    }

    std::cout << "  -> PASS: Both sync and async handlers work seamlessly.\n";
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "   AEGON RADIX TREE ROUTER & ROUTE GROUP TEST SUITE    \n";
    std::cout << "=======================================================\n\n";

    test_radix_tree_static_routes();
    test_radix_tree_parameters();
    test_radix_tree_wildcards();
    test_route_groups();
    test_method_not_allowed();
    test_async_and_sync_handlers();

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL RADIX ROUTER TESTS PASSED SUCCESSFULLY! <<<\n";
    std::cout << "=======================================================\n\n";
    return 0;
}
