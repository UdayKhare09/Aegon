#include "http/Server.h"
#include "http/v1/Http1Parser.h"
#include "data/uuid/UUIDGenerator.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <thread>
#include <arpa/inet.h>
#include <unistd.h>

using namespace aegon::http;
using namespace aegon::data;

void test_http_parser_and_router() {
    std::cout << "[TEST 1] Testing Http1Parser and Router path matching with UUID...\n";

    Router router;
    bool reached_user_route = false;
    UUID captured_uuid{};

    router.get("/users/:id", [&](Context& ctx) -> aegon::core::Task<void> {
        reached_user_route = true;
        auto id = ctx.param_uuid("id");
        assert(id.has_value());
        captured_uuid = *id;
        ctx.uuid(*id);
        co_return;
    });

    std::string raw_req = 
        "GET /users/550e8400-e29b-41d4-a716-446655440000?foo=bar HTTP/1.1\r\n"
        "Host: aegon.dev\r\n"
        "Accept: application/json\r\n"
        "\r\n";

    Request req;
    size_t consumed = 0;
    auto status = v1::Http1Parser::parse(raw_req, req, consumed);
    assert(status == v1::ParseStatus::Complete);
    (void)status;
    assert(req.method() == Method::GET);
    assert(req.path() == "/users/550e8400-e29b-41d4-a716-446655440000");
    assert(req.query() == "foo=bar");
    assert(req.query_param("foo") == "bar");
    assert(req.headers().get("Host") == "aegon.dev");

    Response res;
    auto match = router.match(req);
    assert(match.route_found);
    assert(match.handler != nullptr);

    Context ctx(req, res);
    auto task = (*match.handler)(ctx);
    task.resume();

    assert(reached_user_route);
    assert(captured_uuid == UUID::from_string("550e8400-e29b-41d4-a716-446655440000"));

    std::string out;
    res.serialize_http1(out);
    assert(out.find("200 OK") != std::string_view::npos);
    assert(out.find("550e8400-e29b-41d4-a716-446655440000") != std::string_view::npos);

    std::cout << "  -> PASS: Http1Parser, Router parameter extraction, and UUID binding verified.\n";
}

void test_live_server_loopback() {
    std::cout << "[TEST 2] Testing live Server over loopback TCP with fluent endpoints...\n";

    Server server;
    server.listen(19876, "127.0.0.1");

    server.get("/health", [](Context& ctx) -> aegon::core::Task<void> {
        ctx.json(R"({"status":"healthy"})");
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
        UUID new_user_id = UUIDGenerator::v7();
        ctx.status(StatusCode::Created).uuid(new_user_id);
        co_return;
    });

    std::thread server_thread([&]() {
        server.run();
    });

    // Allow server thread to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Client requests
    auto send_http_request = [](std::string raw) -> std::string {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        assert(fd >= 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(19876);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        int connect_ret = connect(fd, (sockaddr*)&addr, sizeof(addr));
        assert(connect_ret == 0);
        (void)connect_ret;

        ssize_t s = send(fd, raw.data(), raw.size(), 0);
        assert(s == (ssize_t)raw.size());
        (void)s;

        char buf[2048] = {0};
        ssize_t r = recv(fd, buf, sizeof(buf) - 1, 0);
        assert(r > 0);
        close(fd);
        return std::string(buf, r);
    };

    // 1. Test /health
    {
        std::string req = "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        std::string resp = send_http_request(req);
        assert(resp.find("200 OK") != std::string::npos);
        assert(resp.find("{\"status\":\"healthy\"}") != std::string::npos);
        std::cout << "  -> /health response verified: 200 OK\n";
    }

    // 2. Test /users/:id with valid UUID
    {
        std::string req = "GET /users/018942b7-8d29-77a4-9e32-37d45f4705a2 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        std::string resp = send_http_request(req);
        assert(resp.find("200 OK") != std::string::npos);
        assert(resp.find("018942b7-8d29-77a4-9e32-37d45f4705a2") != std::string::npos);
        std::cout << "  -> /users/:id response verified: 200 OK with UUID\n";
    }

    // 3. Test /users/:id with invalid UUID
    {
        std::string req = "GET /users/not-a-valid-uuid HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        std::string resp = send_http_request(req);
        assert(resp.find("400 Bad Request") != std::string::npos);
        assert(resp.find("Invalid UUID parameter") != std::string::npos);
        std::cout << "  -> /users/:id (invalid) response verified: 400 Bad Request\n";
    }

    // 4. Test POST /users (creates UUID v7)
    {
        std::string req = "POST /users HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        std::string resp = send_http_request(req);
        assert(resp.find("201 Created") != std::string::npos);
        assert(resp.find("{\"uuid\":\"") != std::string::npos);
        std::cout << "  -> POST /users response verified: 201 Created with monotonic UUID v7\n";
    }

    server.stop();
    // Connect dummy to wake up accept
    int dummy = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19876);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    connect(dummy, (sockaddr*)&addr, sizeof(addr));
    close(dummy);

    if (server_thread.joinable()) {
        server_thread.join();
    }

    std::cout << "  -> PASS: Live fluent HTTP server loopback test completed successfully.\n";
}

#include "http/v2/Http2Frame.h"
#include "http/v3/QuicPacket.h"

void test_http2_and_http3_structures() {
    std::cout << "[TEST 3] Testing HTTP/2 frame encoding and HTTP/3 packet structures...\n";

    // 1. HTTP/2 Frame Test
    using namespace aegon::http::v2;
    FrameHeader hdr;
    hdr.length = 128;
    hdr.type = FrameType::HEADERS;
    hdr.flags = Flags::END_HEADERS | Flags::END_STREAM;
    hdr.stream_id = 1;

    uint8_t raw[9];
    hdr.encode(raw);

    auto decoded = FrameHeader::decode(raw);
    assert(decoded.length == 128);
    assert(decoded.type == FrameType::HEADERS);
    assert(decoded.flags == (Flags::END_HEADERS | Flags::END_STREAM));
    assert(decoded.stream_id == 1);
    (void)decoded;
    std::cout << "  -> HTTP/2 9-byte frame header encode/decode verified\n";

    // 2. HTTP/3 QUIC Packet Test
    using namespace aegon::http::v3;
    uint8_t quic_short_packet[] = { 0x40 | 0x01, 0x02, 0x03, 0x04 };
    assert(QuicHeader::is_quic_packet(quic_short_packet));
    (void)quic_short_packet;
    std::cout << "  -> HTTP/3 QUIC fixed-bit datagram validation verified\n";
    std::cout << "  -> PASS: HTTP/2 and HTTP/3 protocol structures verified.\n";
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << "       AEGON HTTP CORE & SERVER TEST SUITE             \n";
    std::cout << "=======================================================\n\n";

    test_http_parser_and_router();
    test_live_server_loopback();
    test_http2_and_http3_structures();

    std::cout << "\n=======================================================\n";
    std::cout << "ALL HTTP CORE TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "=======================================================\n";
    return 0;
}

