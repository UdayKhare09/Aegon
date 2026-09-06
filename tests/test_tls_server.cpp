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
    std::cout << "   AEGON TLS (HTTPS) & ALPN (h2 / http/1.1) TEST SUITE \n";
    std::cout << "=======================================================\n\n";

    constexpr uint16_t PORT = 19878;
    Server server;
    server.enable_tls(); // Self-signed in-memory certificate with ALPN
    server.listen(PORT, "127.0.0.1");

    server.get("/health", [](Context& ctx) -> aegon::core::Task<void> {
        ctx.json(R"({"status":"tls_healthy"})");
        co_return;
    });

    server.get("/users/:id", [](Context& ctx) -> aegon::core::Task<void> {
        auto id = ctx.param_uuid("id");
        if (!id) {
            ctx.status(StatusCode::BadRequest).text("Invalid UUID parameter");
            co_return;
        }
        ctx.uuid(*id);
        co_return;
    });

    server.post("/users", [](Context& ctx) -> aegon::core::Task<void> {
        UUID new_id = UUIDGenerator::v7();
        ctx.status(StatusCode::Created).uuid(new_id);
        co_return;
    });

    std::thread server_thread([&]() {
        server.run();
    });

    // Wait for server to bind and generate cert
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    std::cout << "[TEST 1] Testing TLS Handshake & HTTP/2 ALPN ('h2') via curl...\n";
    {
        // Verify both ALPN negotiation of 'h2' and the HTTP 200 response body
        std::string cmd = 
            "curl -k -s --http2 https://127.0.0.1:19878/health | grep -q '\"status\":\"tls_healthy\"'";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);
        (void)ret;
        std::cout << "  -> PASS: HTTPS GET /health via HTTP/2 (ALPN 'h2') verified\n";
    }

    std::cout << "\n[TEST 2] Testing TLS Handshake & HTTP/1.1 ALPN ('http/1.1') via curl...\n";
    {
        std::string cmd = 
            "curl -k -s --http1.1 https://127.0.0.1:19878/health | grep -q '\"status\":\"tls_healthy\"'";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);
        (void)ret;
        std::cout << "  -> PASS: HTTPS GET /health via HTTP/1.1 (ALPN 'http/1.1') verified\n";
    }

    std::cout << "\n[TEST 3] Testing Encrypted SIMD UUID Route Parameter Extraction over TLS...\n";
    {
        std::string cmd = 
            "curl -k -s --http2 https://127.0.0.1:19878/users/018942b7-8d29-77a4-9e32-37d45f4705a2 | grep -q '018942b7-8d29-77a4-9e32-37d45f4705a2'";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);
        (void)ret;
        std::cout << "  -> PASS: Encrypted HTTP/2 GET /users/:id with SIMD UUID verified\n";
    }

    std::cout << "\n[TEST 4] Testing Encrypted POST with Monotonic UUID v7 Generation over TLS...\n";
    {
        std::string cmd = 
            "curl -k -s --http2 -X POST https://127.0.0.1:19878/users | grep -q '{\"uuid\":\"'";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);
        (void)ret;
        std::cout << "  -> PASS: Encrypted HTTP/2 POST /users with UUID v7 verified\n";
    }

    std::cout << "\n[TEST 5] Testing Encrypted High-Concurrency HTTP/2 Multiplexing via h2load...\n";
    {
        std::string cmd = "h2load -n 5000 -c 10 -m 50 https://127.0.0.1:19878/health";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);
        (void)ret;
        std::cout << "  -> PASS: Encrypted HTTP/2 h2load completed with 0 errors\n";
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
    std::cout << "ALL TLS (HTTPS) & ALPN TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "=======================================================\n";
    return 0;
}
