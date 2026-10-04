#include "http/middleware/AccessLog.h"
#include "http/Server.h"
#include "http/v1/Http1Parser.h"
#include "log/Logger.h"
#include "log/MemorySink.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace aegon;
using namespace aegon::http;
using namespace aegon::http::middleware;

static Response dispatch_req(Router& router, std::string_view raw_req) {
    Request req;
    size_t consumed = 0;
    std::string s(raw_req);
    v1::Http1Parser::parse(s, req, consumed);
    Response res;
    auto task = router.dispatch(req, res, nullptr);
    task.resume();
    return res;
}

void test_default_access_log() {
    std::cout << "Testing default access log...\n";
    auto mem_sink = std::make_shared<log::MemorySink>();
    log::set_sinks({mem_sink});
    log::set_level(log::Level::Trace);

    Router router;
    router.use(access_log());
    router.get("/api/v1/users", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).text("User list");
    });

    Response res = dispatch_req(router, "GET /api/v1/users HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert(res.status() == StatusCode::Ok);

    auto recs = mem_sink->records();
    assert(recs.size() == 1);
    const auto& rec = recs[0];
    assert(rec.level == log::Level::Info);
    assert(rec.message.find("GET /api/v1/users 200 OK") != std::string::npos);
    assert(rec.message.find("[HTTP/1.1]") != std::string::npos);
    std::cout << "  -> PASS: " << rec.message << "\n";
}

void test_status_code_levels() {
    std::cout << "Testing status code level classification...\n";
    auto mem_sink = std::make_shared<log::MemorySink>();
    log::set_sinks({mem_sink});
    log::set_level(log::Level::Trace);

    Router router;
    router.use(access_log({
        .normal_level = log::Level::Info,
        .client_error_level = log::Level::Warn,
        .server_error_level = log::Level::Error
    }));

    router.get("/ok", [](Context& ctx) { ctx.res().status(StatusCode::Ok).text("ok"); });
    router.get("/bad", [](Context& ctx) { ctx.res().status(StatusCode::NotFound).text("not found"); });
    router.get("/err", [](Context& ctx) { ctx.res().status(StatusCode::InternalServerError).text("error"); });

    dispatch_req(router, "GET /ok HTTP/1.1\r\nHost: localhost\r\n\r\n");
    dispatch_req(router, "GET /bad HTTP/1.1\r\nHost: localhost\r\n\r\n");
    dispatch_req(router, "GET /err HTTP/1.1\r\nHost: localhost\r\n\r\n");

    auto recs = mem_sink->records();
    assert(recs.size() == 3);
    assert(recs[0].level == log::Level::Info);
    assert(recs[1].level == log::Level::Warn);
    assert(recs[2].level == log::Level::Error);
    std::cout << "  -> PASS\n";
}

void test_slow_request_detection() {
    std::cout << "Testing slow request detection...\n";
    auto mem_sink = std::make_shared<log::MemorySink>();
    log::set_sinks({mem_sink});
    log::set_level(log::Level::Trace);

    Router router;
    router.use(access_log({
        .slow_threshold = std::chrono::milliseconds(10),
        .slow_level = log::Level::Warn
    }));

    router.get("/fast", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).text("fast");
    });

    router.get("/slow", [](Context& ctx) -> core::Task<void> {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        ctx.res().status(StatusCode::Ok).text("slow");
        co_return;
    });

    dispatch_req(router, "GET /fast HTTP/1.1\r\nHost: localhost\r\n\r\n");
    dispatch_req(router, "GET /slow HTTP/1.1\r\nHost: localhost\r\n\r\n");

    auto recs = mem_sink->records();
    assert(recs.size() == 2);
    // Fast request should be Level::Info
    assert(recs[0].level == log::Level::Info);
    assert(recs[0].message.find("[SLOW]") == std::string::npos);

    // Slow request should be promoted to Level::Warn and have [SLOW] tag
    assert(recs[1].level == log::Level::Warn);
    assert(recs[1].message.find("[SLOW]") != std::string::npos);
    std::cout << "  -> PASS: " << recs[1].message << "\n";
}

void test_filtering_and_skip_paths() {
    std::cout << "Testing path and predicate filtering...\n";
    auto mem_sink = std::make_shared<log::MemorySink>();
    log::set_sinks({mem_sink});
    log::set_level(log::Level::Trace);

    Router router;
    router.use(access_log({
        .skip_paths = {"/health", "/metrics"},
        .skip_if = [](const Context& ctx) {
            return ctx.req().header("x-internal-probe") == "true";
        }
    }));

    router.get("/health", [](Context& ctx) { ctx.res().status(StatusCode::Ok).text("healthy"); });
    router.get("/metrics", [](Context& ctx) { ctx.res().status(StatusCode::Ok).text("metrics"); });
    router.get("/probe", [](Context& ctx) { ctx.res().status(StatusCode::Ok).text("probe"); });
    router.get("/actual", [](Context& ctx) { ctx.res().status(StatusCode::Ok).text("actual"); });

    dispatch_req(router, "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n");
    dispatch_req(router, "GET /metrics HTTP/1.1\r\nHost: localhost\r\n\r\n");
    dispatch_req(router, "GET /probe HTTP/1.1\r\nHost: localhost\r\nx-internal-probe: true\r\n\r\n");
    dispatch_req(router, "GET /actual HTTP/1.1\r\nHost: localhost\r\n\r\n");

    auto recs = mem_sink->records();
    assert(recs.size() == 1);
    assert(recs[0].message.find("/actual") != std::string::npos);
    std::cout << "  -> PASS\n";
}

void test_format_styles() {
    std::cout << "Testing JSON and Custom format styles...\n";
    auto mem_sink = std::make_shared<log::MemorySink>();
    log::set_sinks({mem_sink});
    log::set_level(log::Level::Trace);

    // 1. JSON format
    {
        Router router;
        router.use(access_log({.format = AccessLogFormat::Json}));
        router.get("/api/items", [](Context& ctx) {
            ctx.res().status(StatusCode::Created).json("{\"id\":1}");
        });

        dispatch_req(router, "GET /api/items HTTP/1.1\r\nHost: localhost\r\n\r\n");
        auto recs = mem_sink->records();
        assert(recs.size() == 1);
        const auto& json_str = recs[0].message;
        assert(json_str.find("\"method\":\"GET\"") != std::string::npos);
        assert(json_str.find("\"status\":201") != std::string::npos);
        assert(json_str.find("\"phrase\":\"Created\"") != std::string::npos);
        std::cout << "  -> JSON: " << json_str << "\n";
    }

    mem_sink->clear();

    // 2. Custom format
    {
        Router router;
        router.use(access_log({
            .custom_formatter = [](const Context& ctx, std::chrono::nanoseconds /*duration*/) {
                return std::format("CUSTOM: {} {} -> {}",
                    to_string(ctx.req().method()),
                    ctx.req().path(),
                    ctx.res().status_code());
            }
        }));
        router.get("/custom", [](Context& ctx) {
            ctx.res().status(StatusCode::Ok).text("custom");
        });

        dispatch_req(router, "GET /custom HTTP/1.1\r\nHost: localhost\r\n\r\n");
        auto recs = mem_sink->records();
        assert(recs.size() == 1);
        assert(recs[0].message == "CUSTOM: GET /custom -> 200");
        std::cout << "  -> Custom format: " << recs[0].message << "\n";
    }
    std::cout << "  -> PASS\n";
}

void test_headers_and_ip_logging() {
    std::cout << "Testing headers and client IP extraction...\n";
    auto mem_sink = std::make_shared<log::MemorySink>();
    log::set_sinks({mem_sink});
    log::set_level(log::Level::Trace);

    Router router;
    router.use(access_log({
        .request_headers = {"x-request-id", "user-agent"},
        .response_headers = {"content-type"}
    }));

    router.get("/trace", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).json("{\"trace\":true}");
    });

    dispatch_req(router,
        "GET /trace?filter=active HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "x-forwarded-for: 203.0.113.195, 70.41.3.18\r\n"
        "x-request-id: req-uuid-9999\r\n"
        "user-agent: AegonClient/1.0\r\n\r\n");

    auto recs = mem_sink->records();
    assert(recs.size() == 1);
    const auto& msg = recs[0].message;

    // Verify IP extraction
    assert(msg.find("203.0.113.195") != std::string::npos);
    // Verify query param logging
    assert(msg.find("/trace?filter=active") != std::string::npos);
    // Verify headers
    assert(msg.find("[x-request-id: req-uuid-9999]") != std::string::npos);
    assert(msg.find("[user-agent: AegonClient/1.0]") != std::string::npos);
    assert(msg.find("[res:content-type: application/json]") != std::string::npos);

    std::cout << "  -> PASS: " << msg << "\n";
}

void test_handler_request_logger() {
    std::cout << "Testing RequestLogger inside route handlers...\n";
    auto mem_sink = std::make_shared<log::MemorySink>();
    log::set_sinks({mem_sink});
    log::set_level(log::Level::Trace);

    Router router;
    router.use(access_log());

    router.post("/orders", [](Context& ctx) {
        // Preferred pointer style: ctx.get<RequestLogger>()
        auto* log = ctx.get<RequestLogger>();
        assert(log != nullptr);
        log->info("Order placed: {}", 42);

        // Reference style: ctx.local<RequestLogger>()
        ctx.local<RequestLogger>().warn("Inventory is low for item {}", "widget-a");

        ctx.res().status(StatusCode::Created).text("Order placed");
    });

    // 1. Request with correlation ID
    dispatch_req(router,
        "POST /orders HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "x-request-id: req-order-999\r\n\r\n");

    auto recs = mem_sink->records();
    assert(recs.size() == 3);
    assert(recs[0].level == log::Level::Info);
    assert(recs[0].message == "[req-order-999] [POST /orders] Order placed: 42");
    assert(recs[1].level == log::Level::Warn);
    assert(recs[1].message == "[req-order-999] [POST /orders] Inventory is low for item widget-a");
    assert(recs[2].level == log::Level::Info);
    assert(recs[2].message.find("POST /orders 201 Created") != std::string::npos);

    mem_sink->clear();

    // 2. Request without correlation ID
    dispatch_req(router,
        "POST /orders HTTP/1.1\r\n"
        "Host: localhost\r\n\r\n");

    recs = mem_sink->records();
    assert(recs.size() == 3);
    assert(recs[0].message == "[POST /orders] Order placed: 42");
    assert(recs[1].message == "[POST /orders] Inventory is low for item widget-a");

    std::cout << "  -> PASS\n";
}

int main() {
    std::cout << "Running access_log middleware unit tests...\n";
    test_default_access_log();
    test_status_code_levels();
    test_slow_request_detection();
    test_filtering_and_skip_paths();
    test_format_styles();
    test_headers_and_ip_logging();
    test_handler_request_logger();
    std::cout << "All access_log middleware tests PASSED successfully!\n";
    return 0;
}
