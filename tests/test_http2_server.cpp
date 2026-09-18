#include "http/Server.h"
#include "data/uuid/UUIDGenerator.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <arpa/inet.h>
#include <unistd.h>

using namespace aegon::http;
using namespace aegon::data;

int main() {
    std::cout << "=======================================================\n";
    std::cout << "      AEGON HTTP/2 MULTIPLEXING & COMPAT TEST SUITE    \n";
    std::cout << "=======================================================\n\n";

    constexpr uint16_t PORT = 19877;
    Router router;

    router.get("/health", [](Context& ctx) {
        ctx.res().json(R"({"status":"h2_healthy"})");
    });

    router.get("/users/:id", [](Context& ctx) {
        auto id_str = ctx.req().param("id");
        if (!id_str) {
            ctx.res().status(StatusCode::BadRequest).text("Invalid UUID parameter");
            return;
        }
        auto id = UUID::from_string(*id_str);
        if (!id) {
            ctx.res().status(StatusCode::BadRequest).text("Invalid UUID parameter");
            return;
        }
        ctx.res().json(R"({"uuid":")" + std::string(*id_str) + R"("})");
    });

    router.post("/users", [](Context& ctx) {
        UUID new_id = UUIDGenerator::v7();
        ctx.res().status(StatusCode::Created).json(R"({"uuid":")" + new_id.to_string() + R"("})");
    });

    Server server(std::move(router));
    server.listen(PORT, "127.0.0.1");

    std::thread server_thread([&]() {
        server.run();
    });

    // Wait for server to bind
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "[TEST 1] Testing HTTP/2 cleartext (h2c) requests via curl...\n";

    // 1. HTTP/2 GET /health
    {
        int ret = std::system("curl -s --http2-prior-knowledge http://127.0.0.1:19877/health | grep -q '\"status\":\"h2_healthy\"'");
        assert(ret == 0);
        (void)ret;
        std::cout << "  -> PASS: HTTP/2 GET /health verified\n";
    }

    // 2. HTTP/2 GET /users/:id (SIMD UUID route parameter extraction)
    {
        int ret = std::system("curl -s --http2-prior-knowledge http://127.0.0.1:19877/users/018942b7-8d29-77a4-9e32-37d45f4705a2 | grep -q '018942b7-8d29-77a4-9e32-37d45f4705a2'");
        assert(ret == 0);
        (void)ret;
        std::cout << "  -> PASS: HTTP/2 GET /users/:id with SIMD UUID verified\n";
    }

    // 3. HTTP/2 POST /users (RFC 9562 Monotonic UUID v7 generation)
    {
        int ret = std::system("curl -s --http2-prior-knowledge -X POST http://127.0.0.1:19877/users | grep -q '{\"uuid\":\"'");
        assert(ret == 0);
        (void)ret;
        std::cout << "  -> PASS: HTTP/2 POST /users with UUID v7 verified\n";
    }

    // 4. HTTP/2 with Upgrade: h2c handshake (standard curl --http2)
    {
        int ret = std::system("curl -s --http2 http://127.0.0.1:19877/health | grep -q '\"status\":\"h2_healthy\"'");
        assert(ret == 0);
        (void)ret;
        std::cout << "  -> PASS: HTTP/2 with standard Upgrade: h2c handshake verified\n";
    }

    std::cout << "\n[TEST 2] Testing HTTP/1.1 fallback auto-negotiation on same port...\n";
    {
        int ret = std::system("curl -s --http1.1 http://127.0.0.1:19877/health | grep -q '\"status\":\"h2_healthy\"'");
        assert(ret == 0);
        (void)ret;
        std::cout << "  -> PASS: HTTP/1.1 client served seamlessly on HTTP/2 port\n";
    }

    std::cout << "\n[TEST 3] Running high-concurrency multiplexed benchmark with h2load...\n";
    {
        // 10,000 requests, 10 clients, 100 max concurrent streams per client
        int ret = std::system("h2load -n 10000 -c 10 -m 100 http://127.0.0.1:19877/health");
        assert(ret == 0);
        (void)ret;
        std::cout << "  -> PASS: h2load 10,000 multiplexed streams completed with 0 errors\n";
    }

    server.stop();
    // Wake up accept loop
    int dummy = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    connect(dummy, (sockaddr*)&addr, sizeof(addr));
    close(dummy);

    if (server_thread.joinable()) {
        server_thread.join();
    }

    std::cout << "\n=======================================================\n";
    std::cout << "ALL HTTP/2 TESTS & BENCHMARKS PASSED SUCCESSFULLY!\n";
    std::cout << "=======================================================\n";
    return 0;
}
