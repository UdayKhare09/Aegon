#include "http/Server.h"
#include "http/SseStream.h"
#include "core/EventLoop.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>
#include <arpa/inet.h>
#include <unistd.h>

using namespace aegon::http;

struct TestPayload {
    std::string topic;
    int code{0};
    bool active{false};
};

static std::string exec_cmd(const std::string& cmd) {
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return "";
    std::string out;
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) {
        out += buf;
    }
    pclose(fp);
    return out;
}

// ---------------------------------------------------------------------------
// 1. SseStream Unit Tests (Wire format & API verification)
// ---------------------------------------------------------------------------
void test_sse_stream_unit() {
    std::cout << "[UNIT TEST] Testing SseStream wire framing & serialization... " << std::flush;

    std::vector<std::string> written_frames;
    SseWriteFn mock_writer = [&](std::string frame) -> aegon::core::Task<void> {
        written_frames.push_back(std::move(frame));
        co_return;
    };

    aegon::core::EventLoop loop;
    loop.spawn([&]() -> aegon::core::Task<void> {
        SseStream stream(mock_writer);
        assert(stream.is_open());

        // 1. Named event
        co_await stream.event("alert", "sensor_threshold_exceeded");
        assert(written_frames.size() == 1);
        assert(written_frames.back() == "event: alert\ndata: sensor_threshold_exceeded\n\n");

        // 2. Data only
        co_await stream.data("simple notification");
        assert(written_frames.size() == 2);
        assert(written_frames.back() == "data: simple notification\n\n");

        // 3. Comment line
        co_await stream.comment("ping");
        assert(written_frames.size() == 3);
        assert(written_frames.back() == ": ping\n\n");

        // 4. Retry
        co_await stream.retry(5000);
        assert(written_frames.size() == 4);
        assert(written_frames.back() == "retry: 5000\n\n");

        // 5. JSON event
        TestPayload payload{.topic = "telemetry", .code = 42, .active = true};
        co_await stream.send_json("metric", payload);
        assert(written_frames.size() == 5);
        assert(written_frames.back().starts_with("event: metric\ndata: {"));
        assert(written_frames.back().find(R"("topic":"telemetry")") != std::string::npos);
        assert(written_frames.back().find(R"("code":42)") != std::string::npos);
        assert(written_frames.back().find(R"("active":true)") != std::string::npos);
        assert(written_frames.back().ends_with("\n\n"));

        // 6. Close stream
        stream.close();
        assert(!stream.is_open());

        // Subsequent writes after close are no-ops
        co_await stream.data("should not be sent");
        assert(written_frames.size() == 5);
        loop.stop();
    }());

    loop.run();
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Main test runner
// ---------------------------------------------------------------------------
int main() {
    std::cout << "=======================================================\n";
    std::cout << "      AEGON SERVER-SENT EVENTS (SSE) TEST SUITE        \n";
    std::cout << "        Protocols: HTTP/1.1, HTTP/2, and HTTP/3        \n";
    std::cout << "=======================================================\n\n";

    test_sse_stream_unit();

    constexpr uint16_t H1_H2C_PORT = 19881;
    constexpr uint16_t TLS_H3_PORT  = 19882;

    Router router;

    // Normal non-SSE regression endpoint
    router.get("/health", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).json(R"({"status":"healthy"})");
    });

    // SSE Basic endpoint: event, comment, retry, data
    router.get("/sse/basic", [](Context& ctx) -> aegon::core::Task<void> {
        auto stream = co_await ctx.sse();
        co_await stream.event("greeting", "hello from aegon sse");
        co_await stream.comment("heartbeat-keepalive");
        co_await stream.retry(3000);
        co_await stream.data("standalone-payload");
        stream.close();
    });

    // SSE JSON endpoint
    router.get("/sse/json", [](Context& ctx) -> aegon::core::Task<void> {
        auto stream = co_await ctx.sse();
        TestPayload payload{.topic = "system_status", .code = 200, .active = true};
        co_await stream.send_json("status_event", payload);
    });

    // SSE Multi-event stream: sequence of multiple distinct events
    router.get("/sse/multi", [](Context& ctx) -> aegon::core::Task<void> {
        auto stream = co_await ctx.sse();
        for (int i = 1; i <= 3; ++i) {
            co_await stream.event("counter", std::to_string(i));
        }
    });

    // SSE Handler that exits without explicit stream.close()
    router.get("/sse/no_close", [](Context& ctx) -> aegon::core::Task<void> {
        auto stream = co_await ctx.sse();
        co_await stream.event("auto_finish", "done");
    });

    // Configure Server:
    // Port 19881: Plaintext (HTTP/1.1 and HTTP/2 cleartext h2c)
    // Port 19882: TLS + HTTP/3 (HTTPS with ALPN h2/http1.1, plus UDP QUIC HTTP/3)
    Server server(std::move(router));
    server.listen(H1_H2C_PORT, "127.0.0.1", false)
          .listen_tls(TLS_H3_PORT, "127.0.0.1")
          .enable_tls()
          .enable_http3();

    std::thread server_thread([&]() {
        server.run();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    // -----------------------------------------------------------------------
    // [TEST SET 1]: HTTP/1.1 Cleartext & TLS
    // -----------------------------------------------------------------------
    std::cout << "\n--- [HTTP/1.1 TESTS] ---\n";

    // 1.1 HTTP/1.1 Plaintext GET /sse/basic
    {
        std::cout << "[H1.1] Testing Cleartext SSE /sse/basic... " << std::flush;
        std::string cmd = "curl -s -i --http1.1 http://127.0.0.1:" + std::to_string(H1_H2C_PORT) + "/sse/basic";
        std::string resp = exec_cmd(cmd);

        assert(resp.find("HTTP/1.1 200 OK") != std::string::npos);
        assert(resp.find("content-type: text/event-stream") != std::string::npos ||
               resp.find("Content-Type: text/event-stream") != std::string::npos);
        assert(resp.find("transfer-encoding: chunked") != std::string::npos ||
               resp.find("Transfer-Encoding: chunked") != std::string::npos);
        assert(resp.find("cache-control: no-cache") != std::string::npos ||
               resp.find("Cache-Control: no-cache") != std::string::npos);
        assert(resp.find("event: greeting\ndata: hello from aegon sse\n\n") != std::string::npos);
        assert(resp.find(": heartbeat-keepalive\n\n") != std::string::npos);
        assert(resp.find("retry: 3000\n\n") != std::string::npos);
        assert(resp.find("data: standalone-payload\n\n") != std::string::npos);
        std::cout << "PASSED\n";
    }

    // 1.2 HTTP/1.1 Plaintext GET /sse/json
    {
        std::cout << "[H1.1] Testing Cleartext SSE JSON payload... " << std::flush;
        std::string cmd = "curl -s --http1.1 http://127.0.0.1:" + std::to_string(H1_H2C_PORT) + "/sse/json";
        std::string resp = exec_cmd(cmd);

        assert(resp.find("event: status_event") != std::string::npos);
        assert(resp.find(R"("topic":"system_status")") != std::string::npos);
        assert(resp.find(R"("code":200)") != std::string::npos);
        assert(resp.find(R"("active":true)") != std::string::npos);
        std::cout << "PASSED\n";
    }

    // 1.3 HTTP/1.1 Plaintext GET /sse/multi
    {
        std::cout << "[H1.1] Testing Cleartext Multi-event Stream... " << std::flush;
        std::string cmd = "curl -s --http1.1 http://127.0.0.1:" + std::to_string(H1_H2C_PORT) + "/sse/multi";
        std::string resp = exec_cmd(cmd);

        assert(resp.find("event: counter\ndata: 1\n\n") != std::string::npos);
        assert(resp.find("event: counter\ndata: 2\n\n") != std::string::npos);
        assert(resp.find("event: counter\ndata: 3\n\n") != std::string::npos);
        std::cout << "PASSED\n";
    }

    // 1.4 HTTP/1.1 TLS GET /sse/basic
    {
        std::cout << "[H1.1-TLS] Testing HTTPS SSE /sse/basic... " << std::flush;
        std::string cmd = "curl -k -s -i --http1.1 https://127.0.0.1:" + std::to_string(TLS_H3_PORT) + "/sse/basic";
        std::string resp = exec_cmd(cmd);

        assert(resp.find("HTTP/1.1 200 OK") != std::string::npos);
        assert(resp.find("content-type: text/event-stream") != std::string::npos ||
               resp.find("Content-Type: text/event-stream") != std::string::npos);
        assert(resp.find("event: greeting\ndata: hello from aegon sse\n\n") != std::string::npos);
        assert(resp.find("data: standalone-payload\n\n") != std::string::npos);
        std::cout << "PASSED\n";
    }

    // -----------------------------------------------------------------------
    // [TEST SET 2]: HTTP/2 Cleartext (h2c) & TLS (h2)
    // -----------------------------------------------------------------------
    std::cout << "\n--- [HTTP/2 TESTS] ---\n";

    // 2.1 HTTP/2 Cleartext prior-knowledge GET /sse/basic
    {
        std::cout << "[H2C] Testing Cleartext HTTP/2 SSE /sse/basic... " << std::flush;
        std::string cmd = "curl -s -i --http2-prior-knowledge http://127.0.0.1:" + std::to_string(H1_H2C_PORT) + "/sse/basic";
        std::string resp = exec_cmd(cmd);

        assert(resp.find("HTTP/2 200") != std::string::npos);
        assert(resp.find("content-type: text/event-stream") != std::string::npos);
        assert(resp.find("cache-control: no-cache") != std::string::npos);
        assert(resp.find("event: greeting\ndata: hello from aegon sse\n\n") != std::string::npos);
        assert(resp.find(": heartbeat-keepalive\n\n") != std::string::npos);
        assert(resp.find("retry: 3000\n\n") != std::string::npos);
        assert(resp.find("data: standalone-payload\n\n") != std::string::npos);
        std::cout << "PASSED\n";
    }

    // 2.2 HTTP/2 Cleartext prior-knowledge GET /sse/json
    {
        std::cout << "[H2C] Testing Cleartext HTTP/2 SSE JSON payload... " << std::flush;
        std::string cmd = "curl -s --http2-prior-knowledge http://127.0.0.1:" + std::to_string(H1_H2C_PORT) + "/sse/json";
        std::string resp = exec_cmd(cmd);

        assert(resp.find("event: status_event") != std::string::npos);
        assert(resp.find(R"("topic":"system_status")") != std::string::npos);
        assert(resp.find(R"("code":200)") != std::string::npos);
        std::cout << "PASSED\n";
    }

    // 2.3 HTTP/2 Cleartext prior-knowledge GET /sse/multi
    {
        std::cout << "[H2C] Testing Cleartext HTTP/2 Multi-event Stream... " << std::flush;
        std::string cmd = "curl -s --http2-prior-knowledge http://127.0.0.1:" + std::to_string(H1_H2C_PORT) + "/sse/multi";
        std::string resp = exec_cmd(cmd);

        assert(resp.find("event: counter\ndata: 1\n\n") != std::string::npos);
        assert(resp.find("event: counter\ndata: 2\n\n") != std::string::npos);
        assert(resp.find("event: counter\ndata: 3\n\n") != std::string::npos);
        std::cout << "PASSED\n";
    }

    // 2.4 HTTP/2 TLS GET /sse/basic
    {
        std::cout << "[H2-TLS] Testing HTTPS HTTP/2 SSE /sse/basic... " << std::flush;
        std::string cmd = "curl -k -s -i --http2 https://127.0.0.1:" + std::to_string(TLS_H3_PORT) + "/sse/basic";
        std::string resp = exec_cmd(cmd);

        assert(resp.find("HTTP/2 200") != std::string::npos);
        assert(resp.find("content-type: text/event-stream") != std::string::npos);
        assert(resp.find("event: greeting\ndata: hello from aegon sse\n\n") != std::string::npos);
        assert(resp.find("data: standalone-payload\n\n") != std::string::npos);
        std::cout << "PASSED\n";
    }

    // 2.5 HTTP/2 Handler with no explicit stream.close() terminates cleanly
    {
        std::cout << "[H2] Testing implicit stream close on handler return... " << std::flush;
        std::string cmd = "curl -s --http2-prior-knowledge http://127.0.0.1:" + std::to_string(H1_H2C_PORT) + "/sse/no_close";
        std::string resp = exec_cmd(cmd);

        assert(resp.find("event: auto_finish\ndata: done\n\n") != std::string::npos);
        std::cout << "PASSED\n";
    }

    // -----------------------------------------------------------------------
    // [TEST SET 3]: HTTP/3 (QUIC / nghttp3)
    // -----------------------------------------------------------------------
    std::cout << "\n--- [HTTP/3 TESTS] ---\n";

    // 3.1 HTTP/3 GET /sse/basic
    {
        std::cout << "[H3] Testing HTTP/3 SSE /sse/basic... " << std::flush;
        std::string cmd = "curl -k -s -i --http3-only https://127.0.0.1:" + std::to_string(TLS_H3_PORT) + "/sse/basic";
        std::string resp = exec_cmd(cmd);

        assert(resp.find("HTTP/3 200") != std::string::npos);
        assert(resp.find("content-type: text/event-stream") != std::string::npos);
        assert(resp.find("cache-control: no-cache") != std::string::npos);
        assert(resp.find("event: greeting\ndata: hello from aegon sse\n\n") != std::string::npos);
        assert(resp.find(": heartbeat-keepalive\n\n") != std::string::npos);
        assert(resp.find("retry: 3000\n\n") != std::string::npos);
        assert(resp.find("data: standalone-payload\n\n") != std::string::npos);
        std::cout << "PASSED\n";
    }

    // 3.2 HTTP/3 GET /sse/json
    {
        std::cout << "[H3] Testing HTTP/3 SSE JSON payload... " << std::flush;
        std::string cmd = "curl -k -s --http3-only https://127.0.0.1:" + std::to_string(TLS_H3_PORT) + "/sse/json";
        std::string resp = exec_cmd(cmd);

        assert(resp.find("event: status_event") != std::string::npos);
        assert(resp.find(R"("topic":"system_status")") != std::string::npos);
        assert(resp.find(R"("code":200)") != std::string::npos);
        assert(resp.find(R"("active":true)") != std::string::npos);
        std::cout << "PASSED\n";
    }

    // 3.3 HTTP/3 GET /sse/multi
    {
        std::cout << "[H3] Testing HTTP/3 Multi-event Stream... " << std::flush;
        std::string cmd = "curl -k -s --http3-only https://127.0.0.1:" + std::to_string(TLS_H3_PORT) + "/sse/multi";
        std::string resp = exec_cmd(cmd);

        assert(resp.find("event: counter\ndata: 1\n\n") != std::string::npos);
        assert(resp.find("event: counter\ndata: 2\n\n") != std::string::npos);
        assert(resp.find("event: counter\ndata: 3\n\n") != std::string::npos);
        std::cout << "PASSED\n";
    }

    // 3.4 HTTP/3 Handler with no explicit stream.close() terminates cleanly
    {
        std::cout << "[H3] Testing implicit stream close on handler return... " << std::flush;
        std::string cmd = "curl -k -s --http3-only https://127.0.0.1:" + std::to_string(TLS_H3_PORT) + "/sse/no_close";
        std::string resp = exec_cmd(cmd);

        assert(resp.find("event: auto_finish\ndata: done\n\n") != std::string::npos);
        std::cout << "PASSED\n";
    }

    // -----------------------------------------------------------------------
    // [TEST SET 4]: Non-SSE Route Regression Across H1, H2, H3
    // -----------------------------------------------------------------------
    std::cout << "\n--- [REGRESSION TESTS: NON-SSE ROUTES] ---\n";
    {
        std::cout << "[REGRESSION] Testing HTTP/1.1 non-SSE /health... " << std::flush;
        std::string resp = exec_cmd("curl -s --http1.1 http://127.0.0.1:" + std::to_string(H1_H2C_PORT) + "/health");
        assert(resp.find(R"("status":"healthy")") != std::string::npos);
        std::cout << "PASSED\n";

        std::cout << "[REGRESSION] Testing HTTP/2 non-SSE /health... " << std::flush;
        resp = exec_cmd("curl -s --http2-prior-knowledge http://127.0.0.1:" + std::to_string(H1_H2C_PORT) + "/health");
        assert(resp.find(R"("status":"healthy")") != std::string::npos);
        std::cout << "PASSED\n";

        std::cout << "[REGRESSION] Testing HTTP/3 non-SSE /health... " << std::flush;
        resp = exec_cmd("curl -k -s --http3-only https://127.0.0.1:" + std::to_string(TLS_H3_PORT) + "/health");
        assert(resp.find(R"("status":"healthy")") != std::string::npos);
        std::cout << "PASSED\n";
    }

    // Cleanup & stop server
    server.stop();
    // Wake up accept loops
    {
        int fd1 = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a1{};
        a1.sin_family = AF_INET;
        a1.sin_port = htons(H1_H2C_PORT);
        inet_pton(AF_INET, "127.0.0.1", &a1.sin_addr);
        connect(fd1, (sockaddr*)&a1, sizeof(a1));
        close(fd1);

        int fd2 = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a2{};
        a2.sin_family = AF_INET;
        a2.sin_port = htons(TLS_H3_PORT);
        inet_pton(AF_INET, "127.0.0.1", &a2.sin_addr);
        connect(fd2, (sockaddr*)&a2, sizeof(a2));
        close(fd2);
    }

    if (server_thread.joinable()) {
        server_thread.join();
    }

    std::cout << "\n=======================================================\n";
    std::cout << "   ALL SSE TESTS PASSED ACROSS H1, H2, AND H3!         \n";
    std::cout << "=======================================================\n";
    return 0;
}
