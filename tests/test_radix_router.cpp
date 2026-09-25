#include "http/Router.h"
#include "http/Request.h"
#include "http/Response.h"
#include "http/Context.h"
#include "http/v1/Http1Parser.h"
#include "data/types/UUIDGenerator.h"
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
using aegon::data::UUID;

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
        auto id_str = ctx.req().param("id");
        TEST_CHECK(id_str.has_value());
        ctx.res().status(StatusCode::Ok).text(std::string(*id_str));
    });

    router.get("/users/:user_id/posts/:post_id", [](Context& ctx) {
        auto uid = ctx.req().param("user_id");
        auto pid = ctx.req().param("post_id");
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
        auto id_str = ctx.req().param("id");
        TEST_CHECK(id_str.has_value());
        auto extracted_uuid = UUID::from_string(*id_str);
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
        auto path = ctx.req().param("filepath");
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

void test_static_files() {
    std::cout << "[Test 7] Testing static file serving, precompression & RAM cache mtime revalidation...\n";

    std::string tmp_dir = "/tmp/aegon_static_test_" + std::to_string(::getpid());
    ::mkdir(tmp_dir.c_str(), 0755);

    std::string css_file = tmp_dir + "/style.css";
    std::string br_file = tmp_dir + "/style.css.br";
    std::string gz_file = tmp_dir + "/style.css.gz";

    {
        std::ofstream f(css_file);
        f << "body { color: red; }";
    }
    {
        std::ofstream f(br_file);
        f << "BR_COMPRESSED_DATA";
    }
    {
        std::ofstream f(gz_file);
        f << "GZ_COMPRESSED_DATA";
    }

    Router router;
    router.static_files("/static", tmp_dir);

    // 1. Uncompressed GET
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/static/style.css");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
        TEST_CHECK(resp.status() == StatusCode::Ok);
        TEST_CHECK(resp.body() == "body { color: red; }");
        TEST_CHECK(resp.headers().get("Content-Type") == "text/css; charset=utf-8" || resp.headers().get("Content-Type") == "text/css");
    }

    // 2. Precompressed Brotli
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/static/style.css");
        req.headers().set("Accept-Encoding", "gzip, deflate, br");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
        TEST_CHECK(resp.status() == StatusCode::Ok);
        TEST_CHECK(resp.body() == "BR_COMPRESSED_DATA");
        TEST_CHECK(resp.headers().get("Content-Encoding") == "br");
    }

    // 3. Precompressed Gzip
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/static/style.css");
        req.headers().set("Accept-Encoding", "gzip, deflate");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
        TEST_CHECK(resp.status() == StatusCode::Ok);
        TEST_CHECK(resp.body() == "GZ_COMPRESSED_DATA");
        TEST_CHECK(resp.headers().get("Content-Encoding") == "gzip");
    }

    // 4. Memory cache invalidation on disk modification (mtime tracking)
    {
        // Modify file on disk with a tiny sleep to ensure mtime changes
        ::usleep(10000); // 10ms
        {
            std::ofstream f(css_file, std::ios::trunc);
            f << "body { color: blue; }";
        }
        Request req;
        req.set_method(Method::GET);
        req.set_path("/static/style.css");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
        TEST_CHECK(resp.body() == "body { color: blue; }");
    }

    // 5. Path traversal security guard
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/static/../etc/passwd");
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
        TEST_CHECK(resp.status() == StatusCode::NotFound);
    }

    // Cleanup
    ::unlink(css_file.c_str());
    ::unlink(br_file.c_str());
    ::unlink(gz_file.c_str());
    ::rmdir(tmp_dir.c_str());

    std::cout << "  -> PASS: Static files serving, sidecar precompression & mtime cache revalidation verified.\n";
}

void test_http_subsystem_fixes() {
    std::cout << "[Test 8] Testing HTTP subsystem bug fixes & RFC compliance...\n";

    // 1. HeaderMap remove and empty()
    {
        HeaderMap headers;
        TEST_CHECK(headers.empty());
        headers.set("Content-Type", "application/json");
        headers.set("X-Custom-1", "val1");
        headers.set("X-Custom-2", "val2");
        TEST_CHECK(!headers.empty());
        TEST_CHECK(headers.size() == 3);

        bool removed = headers.remove("x-custom-1");
        TEST_CHECK(removed);
        TEST_CHECK(!headers.contains("X-Custom-1"));
        TEST_CHECK(headers.size() == 2);

        headers.remove("Content-Type");
        headers.remove("X-Custom-2");
        TEST_CHECK(headers.empty());
        TEST_CHECK(headers.size() == 0);
    }

    // 2. Trailing slash symmetrical normalization in RadixTree
    {
        Router router;
        bool called = false;
        router.get("/users/:id/", [&](Context& ctx) {
            called = true;
            TEST_CHECK(ctx.req().param("id") == "42");
            ctx.res().text("ok");
        });

        Request req;
        req.set_method(Method::GET);
        req.set_path("/users/42"); // Request without trailing slash should match route registered with trailing slash
        auto res = router.match(req);
        TEST_CHECK(res.route_found);
        Response resp;
        Context ctx(req, resp);
        invoke_handler(*res.handler, ctx);
        TEST_CHECK(called);
    }

    // 3. Response pointer stability across reallocation (SSO strings in deque)
    {
        Response res;
        for (int i = 0; i < 64; ++i) {
            res.set_header_owned("X-Custom-" + std::to_string(i), "v" + std::to_string(i));
        }
        for (int i = 0; i < 64; ++i) {
            auto val = res.headers().get("X-Custom-" + std::to_string(i));
            TEST_CHECK(val.has_value());
            TEST_CHECK(*val == ("v" + std::to_string(i)));
        }
    }

    // 4. RFC 9110: 204 No Content does not emit Content-Length
    {
        Response res;
        res.status(StatusCode::NoContent);
        std::string raw;
        res.serialize_http1(raw);
        TEST_CHECK(raw.find("204 No Content") != std::string::npos);
        TEST_CHECK(raw.find("Content-Length") == std::string::npos);
    }

    // 5. Context::problem() preserves Content-Type: application/problem+json
    {
        Request req;
        req.set_path("/test-problem");
        Response res;
        Context ctx(req, res);
        ctx.problem(StatusCode::BadRequest, "Invalid Parameter", "id must be positive");
        TEST_CHECK(res.status() == StatusCode::BadRequest);
        auto ct = res.headers().get("Content-Type");
        TEST_CHECK(ct.has_value());
        TEST_CHECK(*ct == "application/problem+json");
    }

    // 6. RFC 9110: 405 Method Not Allowed must emit Allow header
    {
        Router router;
        router.get("/api/items", [](Context& ctx) { ctx.res().text("get"); });
        router.post("/api/items", [](Context& ctx) { ctx.res().text("post"); });

        Request req;
        req.set_method(Method::DELETE);
        req.set_path("/api/items");
        Response res;

        auto task = router.dispatch(req, res, nullptr);
        task.resume();

        TEST_CHECK(res.status() == StatusCode::MethodNotAllowed);
        auto allow = res.headers().get("Allow");
        TEST_CHECK(allow.has_value());
        TEST_CHECK(allow->find("GET") != std::string_view::npos);
        TEST_CHECK(allow->find("POST") != std::string_view::npos);
    }

    // 7. RFC 9112: 414 URI Too Long and 431 Headers Too Large
    {
        Request req;
        size_t consumed = 0;
        std::string long_uri = "GET /" + std::string(9000, 'a') + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
        auto st1 = v1::Http1Parser::parse(long_uri, req, consumed);
        TEST_CHECK(st1 == v1::ParseStatus::UriTooLong);

        std::string huge_headers = "GET / HTTP/1.1\r\nHost: localhost\r\n";
        for (int i = 0; i < 700; ++i) {
            huge_headers += "X-Large-" + std::to_string(i) + ": " + std::string(100, 'x') + "\r\n";
        }
        huge_headers += "\r\n";
        auto st2 = v1::Http1Parser::parse(huge_headers, req, consumed);
        TEST_CHECK(st2 == v1::ParseStatus::HeadersTooLarge);

        // 8. RFC 9112 / RFC 9110: 413 Payload Too Large
        std::string huge_cl = "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 20000000\r\n\r\n";
        auto st3 = v1::Http1Parser::parse(huge_cl, req, consumed);
        TEST_CHECK(st3 == v1::ParseStatus::PayloadTooLarge);
    }

    std::cout << "  -> PASS: All HTTP subsystem fixes & RFC compliance checks verified.\n";
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
    test_static_files();
    test_http_subsystem_fixes();

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL RADIX ROUTER TESTS PASSED SUCCESSFULLY! <<<\n";
    std::cout << "=======================================================\n\n";
    return 0;
}
