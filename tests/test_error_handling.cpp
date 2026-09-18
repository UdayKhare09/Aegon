#include "http/Server.h"
#include "http/v1/Http1Parser.h"
#include "http/ProblemDetails.h"
#include "core/EventLoop.h"
#include <iostream>
#include <cassert>
#include <stdexcept>
#include <string>

using namespace aegon::http;

void test_startup_misconfiguration() {
    std::cout << "[TEST 1] Testing startup misconfiguration diagnostics...\n";

    // 1. Invalid port 0
    {
        Server server;
        bool threw = false;
        try {
            server.listen(0);
        } catch (const std::runtime_error& e) {
            threw = true;
            assert(std::string(e.what()).find("port must be greater than 0") != std::string::npos);
        }
        assert(threw);
    }

    // 2. Non-existent TLS certificates
    {
        Server server;
        bool threw = false;
        try {
            server.enable_tls("/nonexistent/path/cert.pem", "/nonexistent/path/key.pem");
        } catch (const std::runtime_error& e) {
            threw = true;
            assert(std::string(e.what()).find("Server TLS configuration error") != std::string::npos);
        }
        assert(threw);
    }

    std::cout << "  -> PASS: Startup diagnostics caught port & TLS misconfigurations.\n";
}

void test_default_500_recovery() {
    std::cout << "[TEST 2] Testing unhandled exception recovery (HTTP 500 RFC 7807)...\n";

    Router router;
    router.get("/crash", [](Context&) -> aegon::core::Task<void> {
        throw std::runtime_error("Simulated catastrophic business logic failure!");
    });

    Request req;
    std::string raw = "GET /crash HTTP/1.1\r\nHost: localhost\r\n\r\n";
    size_t consumed = 0;
    v1::Http1Parser::parse(raw, req, consumed);

    Response res;
    auto task = router.dispatch(req, res, nullptr);
    task.resume();

    assert(res.status() == StatusCode::InternalServerError);
    assert(res.body().find("RFC 7807") == std::string::npos); // Verify JSON structure
    assert(res.body().find("Internal Server Error") != std::string::npos);
    assert(res.body().find("Simulated catastrophic business logic failure!") != std::string::npos);

    std::cout << "  -> PASS: Unhandled exception caught and converted to RFC 7807 HTTP 500.\n";
}

void test_custom_error_handler() {
    std::cout << "[TEST 3] Testing custom set_error_handler mapping...\n";

    struct DomainConflictException : public std::exception {
        const char* what() const noexcept override {
            return "Resource already exists in domain state";
        }
    };

    Router router;
    router.post("/items", [](Context&) -> aegon::core::Task<void> {
        throw DomainConflictException();
    });

    bool custom_handler_called = false;
    router.set_error_handler([&](Context& ctx, std::exception_ptr ex) -> aegon::core::Task<void> {
        custom_handler_called = true;
        try {
            if (ex) std::rethrow_exception(ex);
        } catch (const DomainConflictException& e) {
            ctx.problem(StatusCode::Conflict, "Conflict", e.what());
        } catch (...) {
            ctx.problem(StatusCode::InternalServerError, "Error", "Unknown");
        }
        co_return;
    });

    Request req;
    std::string raw = "POST /items HTTP/1.1\r\nHost: localhost\r\n\r\n";
    size_t consumed = 0;
    v1::Http1Parser::parse(raw, req, consumed);

    Response res;
    auto task = router.dispatch(req, res, nullptr);
    task.resume();

    assert(custom_handler_called);
    assert(res.status() == StatusCode::Conflict);
    assert(res.body().find("Resource already exists in domain state") != std::string::npos);

    std::cout << "  -> PASS: Custom error handler mapped exception to HTTP 409 Conflict.\n";
}

void test_404_and_405_defaults_and_custom() {
    std::cout << "[TEST 4] Testing 404 (Not Found) & 405 (Method Not Allowed) RFC 7807 defaults...\n";

    Router router;
    router.get("/api/v1/orders", [](Context& ctx) {
        ctx.res().text("orders");
    });

    // 1. Default 404
    {
        Request req;
        std::string raw = "GET /api/v1/unknown HTTP/1.1\r\nHost: localhost\r\n\r\n";
        size_t consumed = 0;
        v1::Http1Parser::parse(raw, req, consumed);

        Response res;
        auto task = router.dispatch(req, res, nullptr);
        task.resume();

        assert(res.status() == StatusCode::NotFound);
        assert(res.body().find("Not Found") != std::string::npos);
        assert(res.body().find("/api/v1/unknown") != std::string::npos);
    }

    // 2. Default 405
    {
        Request req;
        std::string raw = "DELETE /api/v1/orders HTTP/1.1\r\nHost: localhost\r\n\r\n";
        size_t consumed = 0;
        v1::Http1Parser::parse(raw, req, consumed);

        Response res;
        auto task = router.dispatch(req, res, nullptr);
        task.resume();

        assert(res.status() == StatusCode::MethodNotAllowed);
        assert(res.body().find("Method Not Allowed") != std::string::npos);
        assert(res.body().find("DELETE") != std::string::npos);
    }

    // 3. Custom 404 and 405
    bool custom_404_called = false;
    bool custom_405_called = false;

    router.set_not_found_handler([&](Context& ctx) {
        custom_404_called = true;
        ctx.problem(StatusCode::NotFound, "Custom 404", "Route was custom 404 handled");
    });

    router.set_method_not_allowed_handler([&](Context& ctx) {
        custom_405_called = true;
        ctx.problem(StatusCode::MethodNotAllowed, "Custom 405", "Method custom 405 handled");
    });

    {
        Request req;
        std::string raw = "GET /nonexistent HTTP/1.1\r\nHost: localhost\r\n\r\n";
        size_t consumed = 0;
        v1::Http1Parser::parse(raw, req, consumed);

        Response res;
        auto task = router.dispatch(req, res, nullptr);
        task.resume();

        assert(custom_404_called);
        assert(res.status() == StatusCode::NotFound);
        assert(res.body().find("Custom 404") != std::string::npos);
    }

    {
        Request req;
        std::string raw = "POST /api/v1/orders HTTP/1.1\r\nHost: localhost\r\n\r\n";
        size_t consumed = 0;
        v1::Http1Parser::parse(raw, req, consumed);

        Response res;
        auto task = router.dispatch(req, res, nullptr);
        task.resume();

        assert(custom_405_called);
        assert(res.status() == StatusCode::MethodNotAllowed);
        assert(res.body().find("Custom 405") != std::string::npos);
    }

    std::cout << "  -> PASS: 404 and 405 defaults and custom overrides verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "    Running Aegon Error Handling Tests  \n";
    std::cout << "========================================\n";

    test_startup_misconfiguration();
    test_default_500_recovery();
    test_custom_error_handler();
    test_404_and_405_defaults_and_custom();

    std::cout << "========================================\n";
    std::cout << "  ALL ERROR HANDLING TESTS PASSED!      \n";
    std::cout << "========================================\n";
    return 0;
}
