#include "http/Server.h"
#include "http/v3/QuicPacket.h"
#include "http/v3/Http3Server.h"
#include "data/uuid/UUID.h"
#include "data/uuid/UUIDGenerator.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <array>
#include <cstdlib>
#include <arpa/inet.h>

using namespace aegon;
using namespace aegon::http;

void test_quic_packet_header_validation() {
    std::cout << "[Test 1] Testing QUIC packet header validation... " << std::flush;

    // QUIC Long Header packet has fixed bit (0x40) set and first bit (0x80) set -> 0xC0
    uint8_t valid_initial_pkt[] = {
        0xC0,                   // Long header, Initial packet
        0x00, 0x00, 0x00, 0x01, // Version 1 (RFC 9000)
        0x08,                   // DCID len: 8
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // DCID
        0x08,                   // SCID len: 8
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18  // SCID
    };

    assert(v3::QuicHeader::is_quic_packet(valid_initial_pkt));

    ngtcp2_version_cid vc{};
    int rv = ngtcp2_pkt_decode_version_cid(&vc, valid_initial_pkt, sizeof(valid_initial_pkt), 16);
    (void)rv;
    assert(rv == 0);
    assert(vc.version == NGTCP2_PROTO_VER_V1);
    assert(vc.dcidlen == 8);
    assert(vc.scidlen == 8);
    assert(vc.dcid[0] == 0x01);
    assert(vc.scid[0] == 0x11);

    // Invalid packet: fixed bit (0x40) is not set
    uint8_t invalid_pkt[] = { 0x00, 0x01, 0x02, 0x03 };
    assert(!v3::QuicHeader::is_quic_packet(invalid_pkt));
    (void)invalid_pkt;

    std::cout << "PASSED\n";
}

void test_http3_server_live() {
    std::cout << "[Test 2] Testing live HTTP/3 Server with curl --http3... " << std::flush;

    const uint16_t port = 19443;
    Server server;
    server.listen(port)
          .enable_tls()     // Generates in-memory self-signed certificate with ALPN h3/h2/http1.1
          .enable_http3();  // Starts UDP QUIC listener on port 19443

    server.get("/health", [](Context& ctx) -> core::Task<void> {
        ctx.res().status(StatusCode::Ok).json(R"({"status":"ok","engine":"aegon-http3"})");
        co_return;
    });

    server.get("/users/:id", [](Context& ctx) -> core::Task<void> {
        auto id_opt = ctx.req().param("id");
        if (!id_opt) {
            ctx.res().status(StatusCode::BadRequest).json(R"({"error":"missing id"})");
            co_return;
        }
        auto uuid_opt = data::UUID::from_string(*id_opt);
        if (!uuid_opt) {
            ctx.res().status(StatusCode::BadRequest).json(R"({"error":"invalid uuid"})");
            co_return;
        }

        std::string out = R"({"id":")" + uuid_opt->to_string() +
                          R"(","version":)" + std::to_string(uuid_opt->version()) +
                          R"(,"protocol":"HTTP/3"})";
        ctx.res().status(StatusCode::Ok).json(out);
        co_return;
    });

    server.post("/echo", [](Context& ctx) -> core::Task<void> {
        ctx.res().status(StatusCode::Ok).text(std::string(ctx.req().body()));
        co_return;
    });

    // Run server in background thread
    std::thread server_thread([&server]() {
        server.run();
    });

    // Give server time to bind TCP & UDP sockets
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Test 1: Verify HTTP/1.1 TLS response includes Alt-Svc header
    {
        std::string cmd = "curl -k -s -D - https://127.0.0.1:" + std::to_string(port) + "/health --http1.1 -o /dev/null";
        FILE* fp = popen(cmd.c_str(), "r");
        assert(fp != nullptr);
        char line[256];
        bool found_alt_svc = false;
        while (fgets(line, sizeof(line), fp)) {
            std::string l(line);
            if (l.find("alt-svc:") != std::string::npos || l.find("Alt-Svc:") != std::string::npos) {
                if (l.find("h3=\":" + std::to_string(port) + "\"") != std::string::npos) {
                    found_alt_svc = true;
                }
            }
        }
        pclose(fp);
        (void)found_alt_svc;
        assert(found_alt_svc);
    }

    // Test 2: Perform native HTTP/3 request using curl --http3-only
    {
        std::string cmd = "curl -k -s --http3-only https://127.0.0.1:" + std::to_string(port) + "/health";
        FILE* fp = popen(cmd.c_str(), "r");
        assert(fp != nullptr);
        char buf[512];
        std::string resp;
        while (fgets(buf, sizeof(buf), fp)) {
            resp += buf;
        }
        pclose(fp);
        assert(resp.find(R"("engine":"aegon-http3")") != std::string::npos);
    }

    // Test 3: HTTP/3 request with SIMD UUID validation
    {
        auto test_uuid = data::UUIDGenerator::v7();
        std::string uuid_str = test_uuid.to_string();

        std::string cmd = "curl -k -s --http3-only https://127.0.0.1:" + std::to_string(port) + "/users/" + uuid_str;
        FILE* fp = popen(cmd.c_str(), "r");
        assert(fp != nullptr);
        char buf[512];
        std::string resp;
        while (fgets(buf, sizeof(buf), fp)) {
            resp += buf;
        }
        pclose(fp);
        assert(resp.find(uuid_str) != std::string::npos);
        assert(resp.find(R"("protocol":"HTTP/3")") != std::string::npos);
    }

    // Test 4: HTTP/3 POST Echo
    {
        std::string cmd = "curl -k -s --http3-only -X POST -d 'QUIC-Payload-12345' https://127.0.0.1:" + std::to_string(port) + "/echo";
        FILE* fp = popen(cmd.c_str(), "r");
        assert(fp != nullptr);
        char buf[512];
        std::string resp;
        while (fgets(buf, sizeof(buf), fp)) {
            resp += buf;
        }
        pclose(fp);
        assert(resp == "QUIC-Payload-12345");
    }

    server.stop();
    // Wake up io_uring event loop to terminate
    std::string poke_cmd = "curl -k -s https://127.0.0.1:" + std::to_string(port) + "/health > /dev/null 2>&1";
    system(poke_cmd.c_str());

    // Also poke UDP to wake up recvmsg
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock >= 0) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        sendto(udp_sock, "x", 1, 0, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        close(udp_sock);
    }

    if (server_thread.joinable()) {
        server_thread.join();
    }

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "   Aegon HTTP/3 over QUIC RFC Compliance Test Suite   \n";
    std::cout << "   RFC 9114 (HTTP/3), RFC 9000 (QUIC), RFC 9204 (QPACK)\n";
    std::cout << "=======================================================\n\n";

    test_quic_packet_header_validation();
    test_http3_server_live();

    std::cout << "\n>>> ALL HTTP/3 TESTS PASSED SUCCESSFULLY! <<<\n\n";
    return 0;
}
