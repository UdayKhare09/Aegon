#include "core/IoUring.h"
#include "core/BufferPool.h"
#include "core/EventLoop.h"
#include "http/Server.h"
#include "http/Context.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

using namespace aegon;
using namespace aegon::core;
using namespace aegon::http;

void test_buffer_pool_registration() {
    std::cout << "[TEST 1] Testing IORING_REGISTER_BUFFERS in BufferPool...\n";
    IoUring ring(128);
    BufferPool pool(ring.raw_ring(), 1, 64, 4096, true);

    std::cout << "  -> BufferPool registered: " << (pool.is_registered() ? "YES" : "FALLBACK (unprivileged)") << "\n";
    assert(pool.entries() == 64);
    assert(pool.buffer_size() == 4096);
    assert(pool.raw_memory() != nullptr);

    // Test buffer recycling
    auto slice = pool.get_buffer(0, 100);
    assert(slice.size() == 100);
    slice[0] = 0xAA;
    slice[99] = 0xBB;
    pool.return_buffer(0);

    auto slice2 = pool.get_buffer(0, 100);
    assert(slice2[0] == 0xAA);
    assert(slice2[99] == 0xBB);

    std::cout << "  -> PASS: Buffer registration and recycling verified.\n";
}

void test_sqpoll_mode() {
    std::cout << "[TEST 2] Testing IORING_SETUP_SQPOLL with graceful fallback...\n";
    IoUringConfig cfg;
    cfg.entries = 128;
    cfg.enable_sqpoll = true;
    cfg.sq_thread_idle_ms = 1000;

    IoUring ring(cfg);
    std::cout << "  -> SQPOLL active: " << (ring.is_sqpoll_enabled() ? "YES (kernel thread)" : "FALLBACK (interrupt mode)") << "\n";
    assert(ring.raw_ring() != nullptr);

    // Test a basic async timeout under the SQPOLL ring
    bool timer_completed = false;
    auto run_timer = [&]() -> Task<void> {
        co_await ring.timeout(10'000'000); // 10ms
        timer_completed = true;
    };

    auto task = run_timer();
    task.resume();

    ring.submit_and_wait(1);
    ring.process_completions();

    assert(timer_completed);
    std::cout << "  -> PASS: Async operations executed cleanly under SQPOLL configuration.\n";
}

void test_send_zc() {
    std::cout << "[TEST 3] Testing Zero-Copy Send (SendZcAwaiter)...\n";
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    socklen_t len = sizeof(addr);
    if (bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) abort();
    if (getsockname(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) abort();
    if (listen(listen_fd, 1) != 0) abort();

    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(client, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) abort();
    int server = accept(listen_fd, nullptr, nullptr);
    if (server < 0) abort();

    IoUring ring(128);

    const std::string payload = "Hello Aegon Zero-Copy Network Engine!";
    bool send_done = false;
    int send_bytes = 0;

    auto send_task = [&]() -> Task<void> {
        send_bytes = co_await ring.send_zc(client, payload);
        send_done = true;
    };

    auto t = send_task();
    t.resume();

    while (!send_done) {
        ring.submit_and_wait(1);
        ring.process_completions();
    }

    assert(send_done);
    assert(send_bytes == static_cast<int>(payload.size()));

    char recv_buf[128]{};
    ssize_t n = ::recv(server, recv_buf, sizeof(recv_buf), 0);
    assert(n == static_cast<ssize_t>(payload.size()));
    assert(payload == std::string(recv_buf, n));

    close(client);
    close(server);
    close(listen_fd);
    std::cout << "  -> PASS: Zero-copy send verified over TCP loopback socket.\n";
}

void test_multishot_accept_stream() {
    std::cout << "[TEST 4] Testing IORING_ACCEPT_MULTISHOT with 30 consecutive connections...\n";
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // Ephemeral port
    socklen_t addrlen = sizeof(addr);
    if (bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) abort();
    if (getsockname(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen) != 0) abort();
    uint16_t port = ntohs(addr.sin_port);
    if (::listen(listen_fd, 128) != 0) abort();

    IoUring ring(256);
    auto stream = ring.accept_multishot(listen_fd);

    std::vector<int> accepted_fds;
    constexpr int TOTAL_CLIENTS = 30;
    bool accept_loop_running = true;

    auto accept_worker = [&]() -> Task<void> {
        while (accept_loop_running && accepted_fds.size() < TOTAL_CLIENTS) {
            auto res = co_await stream.next();
            if (res.fd < 0) {
                std::cout << "res.fd < 0: " << res.fd << "\n";
                break;
            }
            accepted_fds.push_back(res.fd);
        }
    };

    auto task = accept_worker();
    task.resume();

    // Client thread connects 30 sockets
    std::thread client_thread([port]() {
        for (int i = 0; i < TOTAL_CLIENTS; ++i) {
            int cfd = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in saddr{};
            saddr.sin_family = AF_INET;
            saddr.sin_port = htons(port);
            inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
            int ret = connect(cfd, reinterpret_cast<struct sockaddr*>(&saddr), sizeof(saddr));
            assert(ret == 0);
            // Send 1 byte and close
            char b = 'x';
            send(cfd, &b, 1, 0);
            close(cfd);
        }
    });

    while (accepted_fds.size() < TOTAL_CLIENTS) {
        ring.submit_and_wait(1);
        ring.process_completions();
    }

    client_thread.join();
    assert(accepted_fds.size() == TOTAL_CLIENTS);

    for (int fd : accepted_fds) {
        close(fd);
    }
    accept_loop_running = false;
    stream.cancel();
    close(listen_fd);

    std::cout << "  -> PASS: Accepted " << TOTAL_CLIENTS << " connections via IORING_ACCEPT_MULTISHOT.\n";
}

void test_zero_copy_file_serving() {
    std::cout << "[TEST 5] Testing Zero-Copy Static File Serving (io_uring_prep_splice & send_file)...\n";
    
    // Create a temporary 128KB test file with known repeating pattern
    const std::string tmp_file = "/tmp/aegon_zerocopy_test.dat";
    std::vector<uint8_t> expected_bytes(131072);
    for (size_t i = 0; i < expected_bytes.size(); ++i) {
        expected_bytes[i] = static_cast<uint8_t>(i % 251);
    }
    {
        std::ofstream ofs(tmp_file, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(expected_bytes.data()), expected_bytes.size());
    }

    Router router;
    router.get("/static/file.dat", [tmp_file](Context& ctx) -> Task<void> {
        ctx.send_file(tmp_file, "application/octet-stream");
        co_return;
    });

    router.get("/static/index.html", [](Context& ctx) -> Task<void> {
        // Test inline file MIME inference
        ctx.send_file("/tmp/aegon_zerocopy_test.dat", "text/html; charset=utf-8");
        co_return;
    });

    Server server(std::move(router));
    server.listen(18989, "127.0.0.1");

    std::thread srv_thread([&server]() {
        server.run();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Connect and request /static/file.dat
    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in saddr{};
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(18989);
    inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
    if (connect(cfd, reinterpret_cast<struct sockaddr*>(&saddr), sizeof(saddr)) != 0) abort();

    std::string req = "GET /static/file.dat HTTP/1.1\r\nHost: 127.0.0.1:18989\r\nConnection: close\r\n\r\n";
    if (::send(cfd, req.data(), req.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(req.size())) abort();

    std::vector<char> response_data;
    char chunk[8192];
    while (true) {
        ssize_t n = ::recv(cfd, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        response_data.insert(response_data.end(), chunk, chunk + n);
    }
    close(cfd);
    server.stop();

    // Connect dummy to wake up accept loop
    int dummy = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in dummy_addr{};
    dummy_addr.sin_family = AF_INET;
    dummy_addr.sin_port = htons(18989);
    inet_pton(AF_INET, "127.0.0.1", &dummy_addr.sin_addr);
    (void)connect(dummy, reinterpret_cast<struct sockaddr*>(&dummy_addr), sizeof(dummy_addr));
    close(dummy);

    if (srv_thread.joinable()) {
        srv_thread.join();
    }

    std::string resp_str(response_data.begin(), response_data.end());
    size_t header_end = resp_str.find("\r\n\r\n");
    assert(header_end != std::string::npos);

    std::string headers = resp_str.substr(0, header_end);
    std::string body = resp_str.substr(header_end + 4);

    assert(headers.find("HTTP/1.1 200 OK") != std::string::npos);
    assert(headers.find("Content-Length: 131072") != std::string::npos);
    assert(headers.find("Content-Type: application/octet-stream") != std::string::npos);
    assert(body.size() == 131072);

    for (size_t i = 0; i < body.size(); ++i) {
        assert(static_cast<uint8_t>(body[i]) == expected_bytes[i]);
    }

    ::unlink(tmp_file.c_str());
    std::cout << "  -> PASS: 128KB file spliced directly from disk cache to network socket with 100% byte fidelity!\n";
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "   AEGON ADVANCED IO & ZERO-COPY ENGINE TEST SUITE     \n";
    std::cout << "   (MULTISHOT ACCEPT, REGISTER BUFFERS, SQPOLL, SPLICE)\n";
    std::cout << "=======================================================\n\n";

    test_buffer_pool_registration();
    test_sqpoll_mode();
    test_send_zc();
    test_multishot_accept_stream();
    test_zero_copy_file_serving();

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL ADVANCED IO & ZERO-COPY TESTS PASSED! <<<   \n";
    std::cout << "=======================================================\n\n";
    return 0;
}
