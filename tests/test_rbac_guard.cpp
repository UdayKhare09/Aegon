#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <span>

#include "http/Server.h"
#include "http/Middleware.h"
#include "http/v1/Http1Parser.h"
#include "http/middleware/Auth.h"
#include "http/middleware/RbacGuard.h"

using namespace aegon;
using namespace aegon::http;
using namespace aegon::http::middleware;

// Principal struct satisfying RoleHolder concept
struct RbacUser {
    uint64_t id;
    std::string name;
    std::vector<std::string> roles;

    std::span<const std::string> get_roles() const { return roles; }
};

// Struct WITHOUT get_roles
struct NonRoleUser {
    uint64_t id;
    std::string name;
};

// Static assertions for RoleHolder concept
static_assert(RoleHolder<RbacUser>, "RbacUser must satisfy RoleHolder");
static_assert(!RoleHolder<NonRoleUser>, "NonRoleUser must NOT satisfy RoleHolder");

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

void test_single_role_guard() {
    std::cout << "[TEST] require_role<T>(single_role)..." << std::endl;
    Router router;

    // Upstream auth sets identity based on header
    router.use([](Context& ctx, Next next) -> core::Task<void> {
        auto role_header = ctx.req().header("x-mock-role");
        if (role_header.has_value()) {
            std::vector<std::string> roles;
            if (*role_header == "ADMIN") roles = {"ADMIN"};
            else if (*role_header == "USER") roles = {"USER"};
            ctx.set<RbacUser>(RbacUser{1, "test_user", std::move(roles)});
        }
        co_await next(ctx);
    });

    router.get("/admin/panel", std::vector<MiddlewareFn>{require_role<RbacUser>("ADMIN")}, [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("admin-ok");
    });

    // 1. Success with ADMIN role
    {
        Response res = dispatch_offline(router, "GET /admin/panel HTTP/1.1\r\nX-Mock-Role: ADMIN\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(res.body() == "admin-ok");
    }

    // 2. 403 Forbidden with insufficient role (USER)
    {
        Response res = dispatch_offline(router, "GET /admin/panel HTTP/1.1\r\nX-Mock-Role: USER\r\n\r\n");
        assert(res.status() == StatusCode::Forbidden);
        assert(res.headers().get("content-type") == "application/problem+json");
        assert(res.body().find("\"status\":403") != std::string::npos);
        assert(res.body().find("missing required role 'ADMIN'") != std::string::npos);
    }

    // 3. 403 Forbidden when identity is missing entirely from context
    {
        Response res = dispatch_offline(router, "GET /admin/panel HTTP/1.1\r\n\r\n");
        assert(res.status() == StatusCode::Forbidden);
        assert(res.body().find("principal is missing") != std::string::npos);
    }

    std::cout << "  -> PASS\n";
}

void test_multi_role_and_guard() {
    std::cout << "[TEST] require_role<T>(vector<string>) [AND logic]..." << std::endl;
    Router router;

    router.use([](Context& ctx, Next next) -> core::Task<void> {
        auto profile = ctx.req().header("x-profile");
        if (profile == "superadmin") {
            ctx.set<RbacUser>(RbacUser{1, "super", {"ADMIN", "MODERATOR"}});
        } else if (profile == "admin_only") {
            ctx.set<RbacUser>(RbacUser{2, "admin_user", {"ADMIN"}});
        } else if (profile == "moderator_only") {
            ctx.set<RbacUser>(RbacUser{3, "mod_user", {"MODERATOR"}});
        }
        co_await next(ctx);
    });

    router.get("/critical/action", std::vector<MiddlewareFn>{
        require_role<RbacUser>(std::vector<std::string>{"ADMIN", "MODERATOR"})
    }, [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("action-executed");
    });

    // 1. Both roles present -> 200 OK
    {
        Response res = dispatch_offline(router, "GET /critical/action HTTP/1.1\r\nX-Profile: superadmin\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(res.body() == "action-executed");
    }

    // 2. Only ADMIN present -> 403 Forbidden
    {
        Response res = dispatch_offline(router, "GET /critical/action HTTP/1.1\r\nX-Profile: admin_only\r\n\r\n");
        assert(res.status() == StatusCode::Forbidden);
        assert(res.body().find("missing required role 'MODERATOR'") != std::string::npos);
    }

    // 3. Only MODERATOR present -> 403 Forbidden
    {
        Response res = dispatch_offline(router, "GET /critical/action HTTP/1.1\r\nX-Profile: moderator_only\r\n\r\n");
        assert(res.status() == StatusCode::Forbidden);
        assert(res.body().find("missing required role 'ADMIN'") != std::string::npos);
    }

    // 4. Missing identity -> 403 Forbidden
    {
        Response res = dispatch_offline(router, "GET /critical/action HTTP/1.1\r\n\r\n");
        assert(res.status() == StatusCode::Forbidden);
    }

    std::cout << "  -> PASS\n";
}

void test_any_role_or_guard() {
    std::cout << "[TEST] require_any_role<T>(vector<string>) [OR logic]..." << std::endl;
    Router router;

    router.use([](Context& ctx, Next next) -> core::Task<void> {
        auto role = ctx.req().header("x-role");
        if (role.has_value()) {
            ctx.set<RbacUser>(RbacUser{1, "u", {std::string(*role)}});
        }
        co_await next(ctx);
    });

    router.get("/support/tickets", std::vector<MiddlewareFn>{
        require_any_role<RbacUser>(std::vector<std::string>{"AGENT", "SUPERVISOR", "ADMIN"})
    }, [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).body("tickets-view");
    });

    // 1. AGENT matches -> 200 OK
    {
        Response res = dispatch_offline(router, "GET /support/tickets HTTP/1.1\r\nX-Role: AGENT\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(res.body() == "tickets-view");
    }

    // 2. SUPERVISOR matches -> 200 OK
    {
        Response res = dispatch_offline(router, "GET /support/tickets HTTP/1.1\r\nX-Role: SUPERVISOR\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
    }

    // 3. ADMIN matches -> 200 OK
    {
        Response res = dispatch_offline(router, "GET /support/tickets HTTP/1.1\r\nX-Role: ADMIN\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
    }

    // 4. GUEST does not match any -> 403 Forbidden
    {
        Response res = dispatch_offline(router, "GET /support/tickets HTTP/1.1\r\nX-Role: GUEST\r\n\r\n");
        assert(res.status() == StatusCode::Forbidden);
        assert(res.body().find("requires at least one authorized role") != std::string::npos);
    }

    std::cout << "  -> PASS\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "    Aegon RBAC Guard Tests\n";
    std::cout << "========================================\n";

    test_single_role_guard();
    test_multi_role_and_guard();
    test_any_role_or_guard();

    std::cout << "\nALL RBAC GUARD TESTS PASSED!\n";
    return 0;
}
