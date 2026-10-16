#include "core/engine/EpollEngine.h"
#include "core/BufferPool.h"
#include "core/EventLoop.h"
#include "http/Server.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <vector>

using namespace aegon;
using namespace aegon::core;
using namespace aegon::http;

void test_epoll_engine_basics() {
    std::cout << "[TEST 1] Testing EpollEngine basic creation & timer...\n";
    EngineConfig cfg;
    cfg.type = EngineType::Epoll;
    cfg.entries = 128;

    auto engine = IoEngine::create(cfg);
    assert(engine != nullptr);
    assert(engine->type() == EngineType::Epoll);
    assert(engine->name() == "epoll");

    bool timer_fired = false;
    auto run_timer = [&]() -> Task<void> {
        co_await engine->timeout(10'000'000); // 10ms
        timer_fired = true;
    };

    auto task = run_timer();
    task.resume();

    auto start = std::chrono::steady_clock::now();
    while (!timer_fired) {
        engine->submit_and_wait(1);
        engine->process_completions();
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(2)) {
            assert(false && "Timer timed out in EpollEngine");
        }
    }

    assert(timer_fired);
    std::cout << "  -> PASS: EpollEngine timer completed successfully.\n";
}

void test_epoll_tcp_loopback() {
    std::cout << "[TEST 2] Testing EpollEngine TCP loopback (accept, send, recv)...\n";
    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    assert(listen_fd >= 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    socklen_t len = sizeof(addr);
    assert(bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    assert(getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    uint16_t port = ntohs(addr.sin_port);
    assert(listen(listen_fd, 16) == 0);

    BufferPool pool(1, 64, 4096);
    EngineConfig cfg;
    cfg.type = EngineType::Epoll;
    auto engine = IoEngine::create(cfg, pool);

    int client_accepted_fd = -1;
    std::string received_msg;

    auto server_task = [&]() -> Task<void> {
        auto accept_res = co_await engine->accept_once(listen_fd);
        assert(accept_res.fd >= 0);
        client_accepted_fd = accept_res.fd;

        // Receive using provided buffers
        auto recv_res = co_await engine->recv_provided(client_accepted_fd, pool.bgid());
        assert(recv_res.bytes > 0);
        assert(recv_res.buffer_valid);

        auto slice = pool.get_buffer(recv_res.bid, recv_res.bytes);
        received_msg.assign(reinterpret_cast<const char*>(slice.data()), slice.size());
        pool.return_buffer(recv_res.bid);

        // Echo back
        std::string reply = "ECHO: " + received_msg;
        int sent = co_await engine->send(client_accepted_fd, reply.data(), reply.size());
        assert(sent == static_cast<int>(reply.size()));
    };

    auto s_task = server_task();
    s_task.resume();

    // Client connect
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in c_addr{};
    c_addr.sin_family = AF_INET;
    c_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &c_addr.sin_addr);
    assert(connect(client_fd, reinterpret_cast<sockaddr*>(&c_addr), sizeof(c_addr)) == 0);

    // Send payload from client
    std::string msg = "Hello EpollEngine!";
    assert(send(client_fd, msg.data(), msg.size(), 0) == static_cast<ssize_t>(msg.size()));

    while (received_msg.empty()) {
        engine->submit_and_wait(1);
        engine->process_completions();
    }

    assert(received_msg == msg);

    // Flush any pending completions (such as send awaiter)
    engine->submit_and_wait(0);
    engine->process_completions();

    char echo_buf[64]{};
    ssize_t n = recv(client_fd, echo_buf, sizeof(echo_buf), 0);
    assert(n > 0);
    assert(std::string(echo_buf, n) == "ECHO: Hello EpollEngine!");

    close(client_fd);
    close(client_accepted_fd);
    close(listen_fd);
    std::cout << "  -> PASS: EpollEngine TCP accept, provided-buffer recv, and send verified.\n";
}

void test_epoll_multishot_streams() {
    std::cout << "[TEST 3] Testing EpollEngine multishot accept & multishot recv streams...\n";
    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    assert(listen_fd >= 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    socklen_t len = sizeof(addr);
    assert(bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    assert(getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    uint16_t port = ntohs(addr.sin_port);
    assert(listen(listen_fd, 64) == 0);

    BufferPool pool(1, 128, 4096);
    EngineConfig cfg;
    cfg.type = EngineType::Epoll;
    auto engine = IoEngine::create(cfg, pool);

    auto accept_stream = engine->accept_multishot(listen_fd);
    std::vector<int> accepted_clients;
    constexpr int CLIENT_COUNT = 5;

    auto accept_worker = [&]() -> Task<void> {
        while (accepted_clients.size() < CLIENT_COUNT) {
            auto res = co_await accept_stream.next();
            if (res.fd < 0) break;
            accepted_clients.push_back(res.fd);
        }
    };

    auto a_task = accept_worker();
    a_task.resume();

    // Spawn 5 clients
    std::vector<int> client_socks;
    for (int i = 0; i < CLIENT_COUNT; ++i) {
        int cfd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in saddr{};
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
        assert(connect(cfd, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr)) == 0);
        client_socks.push_back(cfd);
    }

    while (accepted_clients.size() < CLIENT_COUNT) {
        engine->submit_and_wait(1);
        engine->process_completions();
    }

    assert(accepted_clients.size() == CLIENT_COUNT);

    // Test multishot recv stream on first client
    auto recv_stream = engine->recv_multishot_stream(accepted_clients[0], pool.bgid());
    std::vector<std::string> chunks;

    auto recv_worker = [&]() -> Task<void> {
        while (chunks.size() < 3) {
            auto res = co_await recv_stream.next();
            if (res.bytes <= 0) break;
            auto buf = pool.get_buffer(res.bid, res.bytes);
            chunks.emplace_back(reinterpret_cast<const char*>(buf.data()), buf.size());
            pool.return_buffer(res.bid);
        }
    };

    auto r_task = recv_worker();
    r_task.resume();

    for (int i = 0; i < 3; ++i) {
        std::string pkt = "CHUNK_" + std::to_string(i);
        send(client_socks[0], pkt.data(), pkt.size(), 0);
        while (chunks.size() <= static_cast<size_t>(i)) {
            engine->submit_and_wait(1);
            engine->process_completions();
        }
    }

    assert(chunks.size() == 3);
    for (int i = 0; i < 3; ++i) {
        assert(chunks[i] == ("CHUNK_" + std::to_string(i)));
    }

    accept_stream.cancel();
    recv_stream.cancel();

    for (int fd : client_socks) close(fd);
    for (int fd : accepted_clients) close(fd);
    close(listen_fd);

    std::cout << "  -> PASS: Multishot accept & multishot recv verified on EpollEngine.\n";
}

void test_epoll_http_server() {
    std::cout << "[TEST 4] Testing Live HTTP Server on EpollEngine (EventLoop integration)...\n";
    // Set AEGON_ENGINE=epoll to ensure EventLoop uses EpollEngine
    ::setenv("AEGON_ENGINE", "epoll", 1);

    Router router;
    router.get("/ping", [](Context& ctx) {
        ctx.res().text("pong-from-epoll");
    });
    router.post("/echo", [](Context& ctx) {
        ctx.res().text(ctx.req().body());
    });

    constexpr uint16_t PORT = 19890;
    Server server(std::move(router));
    server.listen(PORT, "127.0.0.1");

    std::thread server_thread([&server]() {
        server.run();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Client connection
    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in saddr{};
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
    assert(connect(cfd, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr)) == 0);

    std::string req = "GET /ping HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    assert(send(cfd, req.data(), req.size(), 0) == static_cast<ssize_t>(req.size()));

    std::string resp;
    char buf[512];
    while (true) {
        ssize_t n = recv(cfd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        resp.append(buf, n);
    }
    close(cfd);

    assert(resp.find("HTTP/1.1 200 OK") != std::string::npos);
    assert(resp.find("pong-from-epoll") != std::string::npos);

    server.stop();
    // Wake up accept
    int dummy = socket(AF_INET, SOCK_STREAM, 0);
    connect(dummy, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr));
    close(dummy);

    if (server_thread.joinable()) {
        server_thread.join();
    }

    ::unsetenv("AEGON_ENGINE");
    std::cout << "  -> PASS: Live Aegon HTTP Server successfully ran on EpollEngine!\n";
}

void test_engine_selection_and_fail_fast() {
    std::cout << "[Test 5] Engine Selection Precedence & Fail-Fast Verification\n";

    // 1. Explicit Epoll selection
    {
        ::unsetenv("AEGON_ENGINE");
        EngineConfig cfg;
        cfg.type = EngineType::Epoll;
        auto engine = IoEngine::create(cfg);
        assert(engine->type() == EngineType::Epoll);
    }

    // 2. Explicit Epoll via AEGON_ENGINE
    {
        ::setenv("AEGON_ENGINE", "epoll", 1);
        EngineConfig cfg;
        cfg.type = EngineType::Auto;
        auto engine = IoEngine::create(cfg);
        assert(engine->type() == EngineType::Epoll);
        ::unsetenv("AEGON_ENGINE");
    }

    // 3. Auto-fallback when io_uring fails on invalid flags
    {
        ::unsetenv("AEGON_ENGINE");
        EngineConfig cfg;
        cfg.type = EngineType::Auto;
        cfg.flags = 0x80000000; // Intentionally invalid io_uring setup flag
        auto engine = IoEngine::create(cfg);
        // Should have gracefully fallen back to Epoll
        assert(engine->type() == EngineType::Epoll);
    }

    // 4. Programmatic explicit IoUring: Must throw system_error and NOT fallback
    {
        ::unsetenv("AEGON_ENGINE");
        EngineConfig cfg;
        cfg.type = EngineType::IoUring;
        cfg.flags = 0x80000000; // Intentionally invalid io_uring setup flag
        bool caught = false;
        try {
            auto engine = IoEngine::create(cfg);
        } catch (const std::system_error&) {
            caught = true;
        }
        assert(caught);
    }

    // 5. Explicit AEGON_ENGINE=io_uring: Must throw system_error and NOT fallback
    {
        ::setenv("AEGON_ENGINE", "io_uring", 1);
        EngineConfig cfg;
        cfg.type = EngineType::Auto;
        cfg.flags = 0x80000000; // Intentionally invalid io_uring setup flag
        bool caught = false;
        try {
            auto engine = IoEngine::create(cfg);
        } catch (const std::system_error&) {
            caught = true;
        }
        assert(caught);
        ::unsetenv("AEGON_ENGINE");
    }

    std::cout << "  -> PASS: Precedence hierarchy and fail-fast guarantees verified!\n";
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "         AEGON EPOLL ENGINE VERIFICATION SUITE         \n";
    std::cout << "=======================================================\n\n";

    test_epoll_engine_basics();
    test_epoll_tcp_loopback();
    test_epoll_multishot_streams();
    test_epoll_http_server();
    test_engine_selection_and_fail_fast();

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL EPOLL ENGINE TESTS PASSED CLEANLY! <<<      \n";
    std::cout << "=======================================================\n\n";
    return 0;
}
