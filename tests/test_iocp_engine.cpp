#if defined(_WIN32)

#include "core/engine/IocpEngine.h"
#include "core/EventLoop.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <cstdlib>

using namespace aegon::core;

void test_iocp_engine_basics() {
    std::cout << "[TEST 1] Testing IocpEngine basic creation & timer...\n";
    EngineConfig cfg;
    cfg.type = EngineType::Iocp;

    BufferPool pool(1, 1024, 4096);
    auto engine = IoEngine::create(cfg, pool);

    assert(engine->type() == EngineType::Iocp);
    assert(engine->name() == "iocp");

    bool timer_fired = false;
    auto test_timer = [&]() -> Task<void> {
        co_await engine->timeout(50'000'000ULL); // 50ms
        timer_fired = true;
    };

    auto task = test_timer();
    task.resume();

    auto start = std::chrono::steady_clock::now();
    while (!timer_fired) {
        engine->submit_and_wait(1);
        engine->process_completions();
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    assert(timer_fired);
    assert(elapsed >= 40); // at least ~40ms
    std::cout << "  -> PASS: IocpEngine timer completed successfully in " << elapsed << "ms.\n";
}

void test_iocp_tcp_loopback() {
    std::cout << "[TEST 2] Testing IocpEngine TCP loopback (accept, send, recv)...\n";
    init_sockets();

    EventLoop loop;
    auto& engine = loop.engine();
    assert(engine.type() == EngineType::Iocp);

    // Create listening socket
    SOCKET listen_sock = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    assert(listen_sock != INVALID_SOCKET);

    sockaddr_in saddr{};
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    saddr.sin_port = 0; // Ephemeral port

    int rc = bind(listen_sock, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr));
    assert(rc == 0);

    rc = listen(listen_sock, 128);
    assert(rc == 0);

    socklen_t slen = sizeof(saddr);
    getsockname(listen_sock, reinterpret_cast<sockaddr*>(&saddr), &slen);
    uint16_t port = ntohs(saddr.sin_port);

    bool server_done = false;
    bool client_done = false;

    // Server coroutine
    auto server_coro = [&]() -> Task<void> {
        auto acc = co_await engine.accept_once(listen_sock);
        assert(acc.fd != INVALID_OS_SOCKET);

        // Receive data via provided buffer
        auto recv_res = co_await engine.recv_provided(acc.fd, loop.buffer_pool().bgid());
        assert(recv_res.bytes > 0);
        assert(recv_res.buffer_valid);

        auto slice = loop.buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
        std::string req(reinterpret_cast<const char*>(slice.data()), slice.size());
        assert(req == "ping-from-client");

        loop.buffer_pool().return_buffer(recv_res.bid);

        // Send pong back
        std::string resp = "pong-from-iocp";
        int sent = co_await engine.send(acc.fd, resp);
        assert(sent == static_cast<int>(resp.size()));

        int close_rc = co_await engine.close(acc.fd);
        assert(close_rc == 0);
        server_done = true;
    };

    // Client thread
    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        SOCKET c_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        assert(c_sock != INVALID_SOCKET);

        sockaddr_in srv{};
        srv.sin_family = AF_INET;
        srv.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        srv.sin_port = htons(port);

        int connect_res = connect(c_sock, reinterpret_cast<sockaddr*>(&srv), sizeof(srv));
        assert(connect_res == 0);

        std::string msg = "ping-from-client";
        send(c_sock, msg.data(), static_cast<int>(msg.size()), 0);

        char buf[128];
        int n = recv(c_sock, buf, sizeof(buf), 0);
        assert(n > 0);
        std::string reply(buf, n);
        assert(reply == "pong-from-iocp");

        closesocket(c_sock);
        client_done = true;
    });

    loop.spawn(server_coro());
    while (!server_done) {
        engine.submit_and_wait(1);
        engine.process_completions();
    }

    client_thread.join();
    assert(server_done && client_done);

    closesocket(listen_sock);
    std::cout << "  -> PASS: IocpEngine TCP loopback completed cleanly!\n";
}

void test_iocp_multishot_accept_rearms() {
    std::cout << "[TEST 3] Testing IocpEngine multishot accept re-arming...\n";
    init_sockets();

    EventLoop loop(1024, 8, 4096);
    auto& engine = loop.engine();

    SOCKET listen_sock = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (listen_sock == INVALID_SOCKET) std::abort();

    sockaddr_in saddr{};
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    saddr.sin_port = 0;
    if (bind(listen_sock, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr)) != 0) std::abort();
    if (listen(listen_sock, 8) != 0) std::abort();

    int addr_len = sizeof(saddr);
    if (getsockname(listen_sock, reinterpret_cast<sockaddr*>(&saddr), &addr_len) != 0) std::abort();
    uint16_t port = ntohs(saddr.sin_port);

    bool server_done = false;
    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        for (int i = 0; i < 2; ++i) {
            SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (client == INVALID_SOCKET) std::abort();
            sockaddr_in server_addr{};
            server_addr.sin_family = AF_INET;
            server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            server_addr.sin_port = htons(port);
            if (connect(client, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) != 0) std::abort();
            closesocket(client);
        }
    });

    auto accept_task = [&]() -> Task<void> {
        auto stream = engine.accept_multishot(listen_sock);
        auto first = co_await stream.next();
        auto second = co_await stream.next();
        if (!is_valid_socket(first.fd) || !is_valid_socket(second.fd) || first.fd == second.fd) {
            std::cerr << "Multishot accept did not yield two distinct sockets.\n";
            std::abort();
        }
        (void)(co_await engine.close(first.fd));
        (void)(co_await engine.close(second.fd));
        server_done = true;
    };

    loop.spawn(accept_task());
    while (!server_done) {
        engine.submit_and_wait(1);
        engine.process_completions();
    }

    client_thread.join();
    closesocket(listen_sock);
    std::cout << "  -> PASS: Two accepts were delivered as distinct sockets.\n";
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "          AEGON IOCP ENGINE VERIFICATION SUITE         \n";
    std::cout << "=======================================================\n\n";

    test_iocp_engine_basics();
    test_iocp_tcp_loopback();
    test_iocp_multishot_accept_rearms();

    std::cout << "\n=======================================================\n";
    std::cout << "    >>> ALL IOCP ENGINE TESTS PASSED CLEANLY! <<<      \n";
    std::cout << "=======================================================\n\n";
    return 0;
}

#else
int main() { return 0; }
#endif
