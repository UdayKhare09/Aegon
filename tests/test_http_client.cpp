#include "http/client/HttpClient.h"
#include "http/v1/Http1Parser.h"
#include "http/Server.h"
#include "data/uuid/UUIDGenerator.h"

#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace aegon;
using namespace aegon::http;
using namespace aegon::http::client;

void stop_server(Server& server, uint16_t port, std::thread& server_thread, bool is_h3 = false) {
    server.stop();
    int dummy = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    connect(dummy, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    close(dummy);

    if (is_h3) {
        int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_sock >= 0) {
            sendto(udp_sock, "x", 1, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            close(udp_sock);
        }
    }

    if (server_thread.joinable()) {
        server_thread.join();
    }
}

struct UserDto {
    uint64_t id{0};
    std::string name{};
};

void test_url_parser() {
    std::cout << "[TEST 1] URL Parser..." << std::endl;

    // 1. Standard HTTP URL
    auto u1 = Url::parse("http://localhost:8080/api/users?page=1&limit=10#top");
    assert(u1.has_value());
    assert(u1->scheme() == "http");
    assert(!u1->is_https());
    assert(u1->host() == "localhost");
    assert(u1->port() == 8080);
    assert(u1->has_explicit_port());
    assert(u1->path() == "/api/users");
    assert(u1->query() == "page=1&limit=10");
    assert(u1->fragment() == "top");
    assert(u1->target() == "/api/users?page=1&limit=10");
    assert(u1->host_header() == "localhost:8080");
    assert(u1->origin() == "http://localhost:8080");

    // 2. HTTPS URL with default port
    auto u2 = Url::parse("https://api.example.com/v1/data");
    assert(u2.has_value());
    assert(u2->scheme() == "https");
    assert(u2->is_https());
    assert(u2->host() == "api.example.com");
    assert(u2->port() == 443);
    assert(!u2->has_explicit_port());
    assert(u2->path() == "/v1/data");
    assert(u2->query().empty());
    assert(u2->host_header() == "api.example.com");
    assert(u2->origin() == "https://api.example.com:443");

    // 3. Root URL with implicit path
    auto u3 = Url::parse("http://example.com");
    assert(u3.has_value());
    assert(u3->port() == 80);
    assert(u3->path() == "/");
    assert(u3->target() == "/");

    // 4. Invalid URLs
    assert(!Url::parse("not_a_url").has_value());
    assert(!Url::parse("ftp://example.com").has_value());
    assert(!Url::parse("http://:8080").has_value());
    assert(!Url::parse("http://example.com:abc").has_value());

    std::cout << "  -> PASS\n";
}

void test_response_parser() {
    std::cout << "[TEST 2] Http1Parser::parse_response..." << std::endl;

    // 1. Simple 200 OK with Content-Length
    {
        std::string raw = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello";
        Response res;
        size_t consumed = 0;
        auto st = v1::Http1Parser::parse_response(raw, res, consumed);
        assert(st == v1::ParseStatus::Complete);
        assert(res.status() == StatusCode::Ok);
        assert(res.headers().get("content-type") == "text/plain");
        assert(res.body() == "hello");
        assert(consumed == raw.size());
    }

    // 2. 204 No Content
    {
        std::string raw = "HTTP/1.1 204 No Content\r\nServer: Aegon\r\n\r\n";
        Response res;
        size_t consumed = 0;
        auto st = v1::Http1Parser::parse_response(raw, res, consumed);
        assert(st == v1::ParseStatus::Complete);
        assert(res.status() == StatusCode::NoContent);
        assert(res.body().empty());
    }

    // 3. Chunked Transfer Encoding
    {
        std::string raw = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
        Response res;
        size_t consumed = 0;
        auto st = v1::Http1Parser::parse_response(raw, res, consumed);
        assert(st == v1::ParseStatus::Complete);
        assert(res.status() == StatusCode::Ok);
        assert(res.body() == "hello world");
    }

    // 4. Partial / Incomplete response
    {
        std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\npart";
        Response res;
        size_t consumed = 0;
        auto st = v1::Http1Parser::parse_response(raw, res, consumed);
        assert(st == v1::ParseStatus::NeedMoreData);
    }

    std::cout << "  -> PASS\n";
}

void test_http_loopback() {
    std::cout << "[TEST 3] Plain HTTP Loopback Server & Async Client..." << std::endl;

    constexpr uint16_t PORT = 29876;
    Router router;

    router.get("/hello", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).text("Hello from Aegon Server!");
    });

    router.get("/query", [](Context& ctx) {
        auto q = ctx.req().query_param("q");
        ctx.res().status(StatusCode::Ok).text(q.value_or("none"));
    });

    router.post("/echo-json", [](Context& ctx) {
        auto user = ctx.req().json<UserDto>();
        assert(user.has_value());
        ctx.res().status(StatusCode::Created).json(*user);
    });

    router.get("/protected", [](Context& ctx) {
        auto auth = ctx.req().header("authorization");
        if (auth && *auth == "Bearer secret_jwt_token_456") {
            ctx.res().status(StatusCode::Ok).text("Access Granted");
        } else {
            ctx.res().status(StatusCode::Unauthorized).text("Denied");
        }
    });

    Server server(std::move(router));
    server.listen(PORT, "127.0.0.1");

    std::thread server_thread([&]() {
        server.run();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    HttpClient client;

    // 1. GET /hello
    {
        Response res = client.get("http://127.0.0.1:29876/hello").send_sync();
        assert(res.status() == StatusCode::Ok);
        assert(res.is_success());
        assert(res.version() == HttpVersion::Http1_1);
        assert(res.body() == "Hello from Aegon Server!");
    }

    // 2. GET /query with encoded query parameters
    {
        Response res = client.get("http://127.0.0.1:29876/query")
            .query("q", "hello world & special #chars")
            .send_sync();

        assert(res.status() == StatusCode::Ok);
        assert(res.body().find("hello%20world") != std::string::npos);
        assert(res.body().find("%26") != std::string::npos);
    }

    // 3. POST /echo-json with typed Glaze DTO serialization/deserialization
    {
        UserDto send_user{101, "charlie"};
        Response res = client.post("http://127.0.0.1:29876/echo-json")
            .json(send_user)
            .send_sync();

        assert(res.status() == StatusCode::Created);
        auto recv_user = res.json<UserDto>();
        assert(recv_user.has_value());
        assert(recv_user->id == 101);
        assert(recv_user->name == "charlie");
    }

    // 4. GET /protected with Bearer Auth
    {
        Response res = client.get("http://127.0.0.1:29876/protected")
            .bearer_auth("secret_jwt_token_456")
            .send_sync();

        assert(res.status() == StatusCode::Ok);
        assert(res.body() == "Access Granted");
    }

    client.close();
    stop_server(server, PORT, server_thread);

    std::cout << "  -> PASS\n";
}

void test_https_loopback() {
    std::cout << "[TEST 4] HTTPS (TLS) Loopback Server & Async Client..." << std::endl;

    constexpr uint16_t PORT = 29877;
    Router router;

    router.get("/secure-info", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).json(R"({"status":"encrypted_ok"})");
    });

    Server server(std::move(router));
    server.enable_tls(); // Self-signed in-memory RSA 2048 cert
    server.listen(PORT, "127.0.0.1");

    std::thread server_thread([&]() {
        server.run();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Client with insecure_skip_verify = true to accept the self-signed test certificate
    HttpClient client(ClientConfig{
        .tls = TlsClientOptions{
            .insecure_skip_verify = true
        }
    });

    Response res = client.get("https://127.0.0.1:29877/secure-info").send_sync();
    assert(res.status() == StatusCode::Ok);
    assert(res.body().find("encrypted_ok") != std::string::npos);

    // Negative test: Client with strict verification (insecure_skip_verify = false)
    // must reject self-signed cert
    HttpClient strict_client(ClientConfig{
        .tls = TlsClientOptions{
            .insecure_skip_verify = false
        }
    });

    Response strict_res = strict_client.get("https://127.0.0.1:29877/secure-info").send_sync();
    assert(strict_res.status() == StatusCode::BadGateway);

    client.close();
    strict_client.close();
    stop_server(server, PORT, server_thread);

    std::cout << "  -> PASS\n";
}

void test_async_coroutine_usage() {
    std::cout << "[TEST 5] Async Coroutine send() inside EventLoop..." << std::endl;

    constexpr uint16_t PORT = 29878;
    Router router;
    router.get("/async", [](Context& ctx) {
        ctx.res().text("Async Success");
    });

    Server server(std::move(router));
    server.listen(PORT, "127.0.0.1");
    std::thread server_thread([&]() { server.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    HttpClient client;
    bool async_success = false;

    core::EventLoop loop(256, 128, 4096);
    auto make_task = [&]() -> core::Task<void> {
        Response res = co_await client.get("http://127.0.0.1:29878/async").send();
        assert(res.status() == StatusCode::Ok);
        assert(res.body() == "Async Success");
        async_success = true;
        loop.stop();
    };
    loop.spawn(make_task());
    loop.run();

    assert(async_success);
    client.close();
    stop_server(server, PORT, server_thread);

    std::cout << "  -> PASS\n";
}

void test_http3_loopback() {
    std::cout << "[TEST 6] Native HTTP/3 (QUIC) Loopback Server & Client..." << std::endl;

    constexpr uint16_t PORT = 29879;
    Router router;

    router.get("/h3-health", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).text("Hello HTTP3 from Aegon Server!");
    });

    router.post("/h3-echo", [](Context& ctx) {
        auto user = ctx.req().json<UserDto>();
        assert(user.has_value());
        ctx.res().status(StatusCode::Created).json(*user);
    });

    Server server(std::move(router));
    server.listen(PORT, "127.0.0.1")
          .enable_tls()
          .enable_http3();

    std::thread server_thread([&]() {
        server.run();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    HttpClient client(ClientConfig{
        .tls = TlsClientOptions{
            .insecure_skip_verify = true
        }
    });

    // 1. Sync GET request over HTTP/3
    {
        Response res = client.get("https://127.0.0.1:29879/h3-health")
            .http3()
            .send_sync();

        assert(res.status() == StatusCode::Ok);
        assert(res.version() == HttpVersion::Http3);
        assert(res.body() == "Hello HTTP3 from Aegon Server!");
    }

    // 2. Sync POST with JSON DTO over HTTP/3
    {
        UserDto send_user{42, "quic_hero"};
        Response post_res = client.post("https://127.0.0.1:29879/h3-echo")
            .http3()
            .json(send_user)
            .send_sync();

        assert(post_res.status() == StatusCode::Created);
        assert(post_res.version() == HttpVersion::Http3);
        auto recv_user = post_res.json<UserDto>();
        assert(recv_user.has_value());
        assert(recv_user->id == 42);
        assert(recv_user->name == "quic_hero");
    }

    // 3. Async Coroutine send() over HTTP/3 inside EventLoop
    {
        core::EventLoop loop(256, 128, 4096);
        bool async_h3_done = false;
        auto h3_task = [&]() -> core::Task<void> {
            Response ares = co_await client.get("https://127.0.0.1:29879/h3-health")
                .http3()
                .send();
            assert(ares.status() == StatusCode::Ok);
            assert(ares.version() == HttpVersion::Http3);
            assert(ares.body() == "Hello HTTP3 from Aegon Server!");
            async_h3_done = true;
            loop.stop();
        };
        loop.spawn(h3_task());
        loop.run();
        assert(async_h3_done);
    }

    client.close();
    stop_server(server, PORT, server_thread, true);

    std::cout << "  -> PASS\n";
}

void test_http2_loopback() {
    std::cout << "[TEST 7] Native HTTP/2 (h2c & h2) Loopback Server & Client..." << std::endl;

    // 1. HTTP/2 Cleartext (h2c)
    {
        constexpr uint16_t PORT = 29880;
        Router router;

        router.get("/h2c-health", [](Context& ctx) {
            ctx.res().status(StatusCode::Ok).text("Hello h2c from Aegon Server!");
        });

        router.post("/h2c-echo", [](Context& ctx) {
            auto user = ctx.req().json<UserDto>();
            assert(user.has_value());
            ctx.res().status(StatusCode::Created).json(*user);
        });

        Server server(std::move(router));
        server.listen(PORT, "127.0.0.1");

        std::thread server_thread([&]() {
            server.run();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        HttpClient client;

        // A. Synchronous GET over h2c
        Response res = client.get("http://127.0.0.1:29880/h2c-health")
            .http2()
            .send_sync();

        assert(res.status() == StatusCode::Ok);
        assert(res.version() == HttpVersion::Http2);
        assert(res.body() == "Hello h2c from Aegon Server!");

        // B. Synchronous POST with JSON DTO over h2c
        UserDto send_user{88, "h2c_master"};
        Response post_res = client.post("http://127.0.0.1:29880/h2c-echo")
            .http2()
            .json(send_user)
            .send_sync();

        assert(post_res.status() == StatusCode::Created);
        assert(post_res.version() == HttpVersion::Http2);
        auto recv_user = post_res.json<UserDto>();
        assert(recv_user.has_value());
        assert(recv_user->id == 88);
        assert(recv_user->name == "h2c_master");

        // C. Asynchronous Coroutine send() over h2c inside EventLoop
        core::EventLoop loop(256, 128, 4096);
        bool async_h2c_done = false;
        auto h2c_task = [&]() -> core::Task<void> {
            Response ares = co_await client.get("http://127.0.0.1:29880/h2c-health")
                .http2()
                .send();
            assert(ares.status() == StatusCode::Ok);
            assert(ares.version() == HttpVersion::Http2);
            assert(ares.body() == "Hello h2c from Aegon Server!");
            async_h2c_done = true;
            loop.stop();
        };
        loop.spawn(h2c_task());
        loop.run();
        assert(async_h2c_done);

        client.close();
        stop_server(server, PORT, server_thread);
    }

    // 2. HTTP/2 over TLS (h2)
    {
        constexpr uint16_t PORT = 29881;
        Router router;

        router.get("/h2-secure", [](Context& ctx) {
            ctx.res().status(StatusCode::Ok).text("Encrypted HTTP/2 Success");
        });

        Server server(std::move(router));
        server.enable_tls(); // Self-signed cert with ALPN h2 & http/1.1
        server.listen(PORT, "127.0.0.1");

        std::thread server_thread([&]() {
            server.run();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        HttpClient client(ClientConfig{
            .tls = TlsClientOptions{
                .insecure_skip_verify = true
            }
        });

        Response res = client.get("https://127.0.0.1:29881/h2-secure")
            .http2()
            .send_sync();

        assert(res.status() == StatusCode::Ok);
        assert(res.version() == HttpVersion::Http2);
        assert(res.body() == "Encrypted HTTP/2 Success");

        client.close();
        stop_server(server, PORT, server_thread);
    }

    std::cout << "  -> PASS\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "       Aegon HttpClient Test Suite\n";
    std::cout << "========================================\n";

    test_url_parser();
    test_response_parser();
    test_http_loopback();
    test_https_loopback();
    test_async_coroutine_usage();
    test_http3_loopback();
    test_http2_loopback();

    std::cout << "\nALL HTTP CLIENT TESTS PASSED!\n";
    return 0;
}
