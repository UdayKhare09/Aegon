#include "net/Net.h"
#include "core/EventLoop.h"
#include <cassert>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace aegon::net;
using namespace aegon::core;

void test_socket_address() {
    std::cout << "Testing SocketAddress...\n";

    // IPv4
    auto sa4 = SocketAddress::from_ipv4("127.0.0.1", 8080);
    assert(sa4.has_value());
    assert(sa4->is_ipv4());
    assert(!sa4->is_ipv6());
    assert(sa4->ip() == "127.0.0.1");
    assert(sa4->port() == 8080);
    assert(sa4->to_string() == "127.0.0.1:8080");

    // Port modification
    sa4->set_port(9090);
    assert(sa4->port() == 9090);
    assert(sa4->to_string() == "127.0.0.1:9090");

    // Any IPv4
    auto any4 = SocketAddress::any_ipv4(5555);
    assert(any4.is_ipv4());
    assert(any4.port() == 5555);
    assert(any4.ip() == "0.0.0.0");

    // IPv6
    auto sa6 = SocketAddress::from_ipv6("::1", 443);
    assert(sa6.has_value());
    assert(sa6->is_ipv6());
    assert(!sa6->is_ipv4());
    assert(sa6->ip() == "::1");
    assert(sa6->port() == 443);
    assert(sa6->to_string() == "[::1]:443");

    // Generic parse
    auto p1 = SocketAddress::parse("192.168.1.1", 1234);
    assert(p1.has_value() && p1->is_ipv4());
    auto p2 = SocketAddress::parse("fe80::1", 5678);
    assert(p2.has_value() && p2->is_ipv6());
    auto p_invalid = SocketAddress::parse("invalid-ip", 80);
    assert(!p_invalid.has_value());

    // Equality
    auto sa4_copy = SocketAddress::from_ipv4("127.0.0.1", 9090);
    assert(*sa4 == *sa4_copy);

    std::cout << "SocketAddress tests passed.\n";
}

void test_socket_options() {
    std::cout << "Testing SocketOptions...\n";
    init_sockets();

#if defined(_WIN32)
    os_socket_t s = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
#else
    os_socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
#endif
    assert(is_valid_socket(s));

    assert(SocketOptions::set_nonblocking(s, true));
    assert(SocketOptions::set_reuse_address(s, true));
    assert(SocketOptions::set_tcp_nodelay(s, true));
    assert(SocketOptions::set_tcp_keepalive(s, true, 30, 5, 2));
    assert(SocketOptions::set_send_buffer_size(s, 65536));
    assert(SocketOptions::set_recv_buffer_size(s, 65536));

    close_socket(s);
    std::cout << "SocketOptions tests passed.\n";
}

void test_tcp_listener_and_stream() {
    std::cout << "Testing TcpListener and TcpStream loopback...\n";

    uint16_t port = 19191;
    TcpListenConfig cfg;
    cfg.port = port;
    cfg.reuse_addr = true;
    cfg.reuse_port = true;

    auto listener = TcpListener::bind(port, "127.0.0.1", cfg);
    assert(listener.has_value());
    assert(listener->is_valid());

    EngineConfig ecfg;
    EventLoop loop(ecfg, 256, 4096);

    bool server_received = false;
    bool client_received = false;

    // Server coroutine
    auto server_coro = [&]() -> Task<void> {
        auto client_opt = co_await listener->accept(loop.engine());
        assert(client_opt.has_value());
        auto& stream = *client_opt;
        assert(stream.is_valid());

        char buf[64] = {0};
        int n = co_await stream.recv(buf, sizeof(buf));
        assert(n > 0);
        std::string_view req(buf, n);
        assert(req == "PING_FROM_CLIENT");
        server_received = true;

        std::string_view resp = "PONG_FROM_SERVER";
        int sent = co_await stream.send(resp.data(), resp.size());
        assert(sent == static_cast<int>(resp.size()));

        co_await stream.close();
    };

    // Client coroutine
    auto client_coro = [&]() -> Task<void> {
        auto client_opt = co_await TcpConnector::connect(loop.engine(), "127.0.0.1", port);
        assert(client_opt.has_value());
        auto& stream = *client_opt;
        assert(stream.is_valid());

        std::string_view req = "PING_FROM_CLIENT";
        int sent = co_await stream.send(req.data(), req.size());
        assert(sent == static_cast<int>(req.size()));

        char buf[64] = {0};
        int n = co_await stream.recv(buf, sizeof(buf));
        assert(n > 0);
        std::string_view resp(buf, n);
        assert(resp == "PONG_FROM_SERVER");
        client_received = true;

        co_await stream.close();
        loop.stop();
    };

    loop.spawn(server_coro());
    loop.spawn(client_coro());
    loop.run();

    assert(server_received);
    assert(client_received);
    listener->close();

    std::cout << "TcpListener and TcpStream loopback passed.\n";
}

void test_udp_socket() {
    std::cout << "Testing UdpSocket loopback...\n";

    uint16_t port1 = 19291;
    uint16_t port2 = 19292;

    auto s1 = UdpSocket::bind(port1, "127.0.0.1", true);
    auto s2 = UdpSocket::bind(port2, "127.0.0.1", true);
    assert(s1.has_value() && s1->is_valid());
    assert(s2.has_value() && s2->is_valid());

    EngineConfig ecfg;
    EventLoop loop(ecfg, 256, 4096);

    bool received = false;

    auto recv_coro = [&]() -> Task<void> {
        char buf[64] = {0};
        SocketAddress from;
        int n = co_await s2->recvfrom(loop.engine(), buf, sizeof(buf), from);
        assert(n > 0);
        std::string_view msg(buf, n);
        assert(msg == "HELLO_UDP");
        assert(from.port() == port1);
        received = true;
        loop.stop();
    };

    auto send_coro = [&]() -> Task<void> {
        auto target = SocketAddress::from_ipv4("127.0.0.1", port2);
        assert(target.has_value());
        std::string_view msg = "HELLO_UDP";
        int sent = s1->sendto(msg.data(), msg.size(), *target);
        assert(sent == static_cast<int>(msg.size()));
        co_return;
    };

    loop.spawn(recv_coro());
    loop.spawn(send_coro());
    loop.run();

    assert(received);
    s1->close();
    s2->close();

    std::cout << "UdpSocket loopback passed.\n";
}

void test_udp_server_and_client() {
    std::cout << "Testing UdpServer and UdpClient...\n";

    uint16_t port = 19494;
    EngineConfig ecfg;
    EventLoop loop(ecfg, 256, 4096);

    UdpServer server(loop, port, "127.0.0.1");
    bool server_got_packet = false;

    server.on_packet([&](std::span<const uint8_t> data, const SocketAddress& from, UdpSocket& sock) -> Task<void> {
        std::string_view req(reinterpret_cast<const char*>(data.data()), data.size());
        assert(req == "UDP_REQ");
        server_got_packet = true;

        std::string_view resp = "UDP_RESP";
        sock.sendto(resp.data(), resp.size(), from);
        co_return;
    });

    assert(server.start());

    auto client_opt = UdpClient::create();
    assert(client_opt.has_value());
    auto& client = *client_opt;

    bool client_got_reply = false;

    auto client_coro = [&]() -> Task<void> {
        auto server_addr = *SocketAddress::from_ipv4("127.0.0.1", port);
        std::string_view req = "UDP_REQ";
        std::span<const uint8_t> req_span(reinterpret_cast<const uint8_t*>(req.data()), req.size());

        auto resp_opt = co_await client.send_and_receive(loop.engine(), req_span, server_addr);
        assert(resp_opt.has_value());
        std::string_view resp(reinterpret_cast<const char*>(resp_opt->data()), resp_opt->size());
        assert(resp == "UDP_RESP");
        client_got_reply = true;

        server.stop();
        client.close();
        loop.stop();
    };

    loop.spawn(client_coro());
    loop.run();

    assert(server_got_packet);
    assert(client_got_reply);

    std::cout << "UdpServer and UdpClient passed.\n";
}

void test_tls() {
    std::cout << "Testing TlsContext and TlsStream loopback...\n";

    tls::TlsContext ctx;
    assert(ctx.is_valid());
    bool gen_ok = ctx.generate_self_signed("localhost");
    assert(gen_ok);

    uint16_t port = 19393;
    auto listener = TcpListener::bind(port, "127.0.0.1");
    assert(listener.has_value());

    EngineConfig ecfg;
    EventLoop loop(ecfg, 256, 4096);

    bool server_tls_done = false;

    auto server_coro = [&]() -> Task<void> {
        auto client_opt = co_await listener->accept(loop.engine());
        assert(client_opt.has_value());
        auto raw_fd = client_opt->release();

        tls::TlsStream tls_stream(loop, raw_fd, ctx.native_handle());
        bool ok = co_await tls_stream.handshake();
        assert(ok);

        char buf[64] = {0};
        int n = co_await tls_stream.read_plaintext(buf, sizeof(buf));
        assert(n > 0);
        std::string_view req(buf, n);
        assert(req == "SECRET_ENCRYPTED_DATA");

        std::string_view reply = "TLS_ACK";
        int sent = co_await tls_stream.write_plaintext(reply.data(), reply.size());
        assert(sent == static_cast<int>(reply.size()));

        server_tls_done = true;
        (void)co_await loop.engine().close(raw_fd);
        loop.stop();
    };

    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        os_socket_t sock = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            close_socket(sock);
            return;
        }

        SSL_CTX* client_ctx = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_verify(client_ctx, SSL_VERIFY_NONE, nullptr);
        SSL* client_ssl = SSL_new(client_ctx);
        SSL_set_fd(client_ssl, static_cast<int>(sock));

        if (SSL_connect(client_ssl) == 1) {
            std::string_view msg = "SECRET_ENCRYPTED_DATA";
            SSL_write(client_ssl, msg.data(), static_cast<int>(msg.size()));

            char buf[64] = {0};
            int r = SSL_read(client_ssl, buf, sizeof(buf));
            assert(r > 0);
            assert(std::string_view(buf, r) == "TLS_ACK");

            SSL_shutdown(client_ssl);
        }

        SSL_free(client_ssl);
        SSL_CTX_free(client_ctx);
        close_socket(sock);
    });

    loop.spawn(server_coro());
    loop.run();

    if (client_thread.joinable()) {
        client_thread.join();
    }

    assert(server_tls_done);
    listener->close();

    std::cout << "TlsContext and TlsStream loopback passed.\n";
}

void test_quic_server() {
    std::cout << "Testing QuicServer...\n";

    tls::TlsContext ctx;
    assert(ctx.generate_self_signed("localhost"));

    uint16_t port = 19595;
    EngineConfig ecfg;
    EventLoop loop(ecfg, 256, 4096);

    quic::QuicServer qserver(loop, port, ctx, "127.0.0.1");
    assert(qserver.start());
    assert(qserver.is_running());
    assert(qserver.port() == port);

    // Stop cleanly
    qserver.stop();
    assert(!qserver.is_running());

    std::cout << "QuicServer passed.\n";
}

int main() {
    std::cout << "=== Running Aegon Net Test Suite ===\n";
    test_socket_address();
    test_socket_options();
    test_tcp_listener_and_stream();
    test_udp_socket();
    test_udp_server_and_client();
    test_tls();
    test_quic_server();
    std::cout << "=== All Aegon Net Tests Passed Successfully! ===\n";
    return 0;
}

