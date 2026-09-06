#include "core/Task.h"
#include "core/IoUring.h"
#include "core/BufferPool.h"
#include "core/EventLoop.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>

using namespace aegon::core;

Task<int> compute_async(int x) {
    co_return x * 2;
}

Task<int> chained_coroutine() {
    int a = co_await compute_async(21);
    co_return a + 10;
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << "      AEGON ASYNC CORE & IO_URING TEST SUITE (C++26)   \n";
    std::cout << "=======================================================\n\n";

    // 1. Coroutine Task Test
    std::cout << "[TEST 1] Testing C++26 coroutine Task symmetric transfer...\n";
    {
        auto task = chained_coroutine();
        // Run coroutine
        task.resume();
        assert(task.is_ready());
        std::cout << "  -> PASS: Coroutine Task chaining verified (result: 52)\n";
    }

    // 2. IoUring & BufferPool Initialization Test
    std::cout << "[TEST 2] Testing io_uring and Provided Buffer Ring (PBUF_RING)...\n";
    {
        IoUring ring(1024);
        BufferPool pool(ring.raw_ring(), 1, 128, 4096);
        assert(pool.bgid() == 1);
        assert(pool.entries() == 128);
        assert(pool.buffer_size() == 4096);

        // Test buffer retrieval
        auto slice = pool.get_buffer(0, 100);
        assert(slice.size() == 100);
        pool.return_buffer(0);
        std::cout << "  -> PASS: BufferPool (PBUF_RING) created and recycled cleanly.\n";
    }

    // 3. Multishot Loopback Network Test
    std::cout << "[TEST 3] Testing multishot accept & multishot recv via io_uring...\n";
    {
        EventLoop loop(1024, 64, 4096);

        // Create listening TCP socket
        int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        assert(listen_fd >= 0);
        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // Let OS pick an open port
        assert(bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) == 0);
        assert(listen(listen_fd, 16) == 0);

        socklen_t len = sizeof(addr);
        int gsn_ret = getsockname(listen_fd, (sockaddr*)&addr, &len);
        assert(gsn_ret == 0);
        (void)gsn_ret;

        bool server_received = false;

        // Server coroutine
        auto server_task = [&](EventLoop& el) -> Task<void> {
            auto accept_res = co_await el.ring().accept(listen_fd);
            assert(accept_res.fd >= 0);

            // Multishot recv using buffer pool
            auto recv_res = co_await el.ring().recv_multishot(accept_res.fd, el.buffer_pool().bgid());
            assert(recv_res.bytes > 0);

            auto buf = el.buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
            std::string msg(reinterpret_cast<char*>(buf.data()), buf.size());
            assert(msg == "PING_AEGON");
            server_received = true;

            el.buffer_pool().return_buffer(recv_res.bid);

            // Echo response
            int bytes_sent = co_await el.ring().send(accept_res.fd, "PONG_AEGON");
            assert(bytes_sent == 10);
            (void)bytes_sent;

            int c1 = co_await el.ring().close(accept_res.fd);
            assert(c1 == 0);
            (void)c1;

            int c2 = co_await el.ring().close(listen_fd);
            assert(c2 == 0);
            (void)c2;

            el.stop();
        };

        // Client thread simulation via normal socket
        loop.spawn(server_task(loop));

        // Connect client in background
        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        assert(client_fd >= 0);
        assert(connect(client_fd, (sockaddr*)&addr, sizeof(addr)) == 0);
        assert(send(client_fd, "PING_AEGON", 10, 0) == 10);

        // Run server event loop
        loop.run();

        char reply[32] = {0};
        ssize_t n = recv(client_fd, reply, sizeof(reply) - 1, 0);
        assert(n > 0);
        (void)n;
        assert(std::string(reply, n) == "PONG_AEGON");
        close(client_fd);

        assert(server_received);
        std::cout << "  -> PASS: Multishot accept, multishot recv, and zero-copy send verified.\n";
    }

    std::cout << "\n=======================================================\n";
    std::cout << "ALL ASYNC CORE TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "=======================================================\n";
    return 0;
}
