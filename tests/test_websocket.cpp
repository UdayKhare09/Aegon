#include "http/websocket/WebSocketFrame.h"
#include "http/websocket/WebSocketHandshake.h"
#include "http/websocket/WebSocketSession.h"
#include "http/Server.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace aegon::http;
using namespace aegon::http::websocket;

// 1. Test RFC 6455 Handshake calculation with official spec vector
void test_handshake_rfc_vector() {
    std::cout << "[TEST 1] Testing RFC 6455 §1.3 Sec-WebSocket-Accept test vector...\n";
    std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
    std::string accept = compute_websocket_accept(key);
    assert(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    std::string res = build_handshake_response(accept);
    assert(res.find("HTTP/1.1 101 Switching Protocols\r\n") != std::string::npos);
    assert(res.find("Upgrade: websocket\r\n") != std::string::npos);
    assert(res.find("Connection: Upgrade\r\n") != std::string::npos);
    assert(res.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n") != std::string::npos);
    std::cout << "  -> Passed: " << accept << "\n";
}

// 2. Test Frame Header Parsing and Serializing
void test_frame_header_parsing_and_serializing() {
    std::cout << "[TEST 2] Testing frame header parsing, serialization, and SIMD unmasking...\n";

    // Text frame header
    uint8_t out[10]{};
    size_t hlen = serialize_frame_header(Opcode::Text, 5, out, true);
    assert(hlen == 2);
    assert(out[0] == 0x81); // FIN + Text
    assert(out[1] == 0x05); // No mask, length 5

    // Medium payload (126)
    hlen = serialize_frame_header(Opcode::Binary, 300, out, true);
    assert(hlen == 4);
    assert(out[0] == 0x82); // FIN + Binary
    assert(out[1] == 126);

    // Large payload (127)
    hlen = serialize_frame_header(Opcode::Binary, 70000, out, true);
    assert(hlen == 10);
    assert(out[0] == 0x82);
    assert(out[1] == 127);

    // Parse masked client text frame: "Hello" masked with 0x37fa213d
    // Key: 37 fa 21 3d
    // "Hello" = 48 65 6c 6c 6f
    // Masked = 48^37, 65^fa, 6c^21, 6c^3d, 6f^37 = 7f 9f 4d 51 58
    uint8_t client_frame[] = {
        0x81, 0x85, // FIN + text, Masked + len 5
        0x37, 0xfa, 0x21, 0x3d, // Mask key
        0x7f, 0x9f, 0x4d, 0x51, 0x58  // Masked "Hello"
    };

    FrameHeader header{};
    std::string_view frame_sv(reinterpret_cast<const char*>(client_frame), sizeof(client_frame));
    auto parse_res = parse_frame_header(frame_sv, header);
    assert(parse_res == FrameParseResult::Complete);
    assert(header.fin == true);
    assert(header.opcode == Opcode::Text);
    assert(header.masked == true);
    assert(header.payload_len == 5);
    assert(header.header_len == 6);

    // Test in-place SIMD unmasking
    uint8_t payload[5] = {0x7f, 0x9f, 0x4d, 0x51, 0x58};
    unmask_payload_inplace(payload, 5, header.mask_key);
    assert(std::string_view(reinterpret_cast<const char*>(payload), 5) == "Hello");

    // Test across all size boundaries (Tiers 1-5: 0 to 135 bytes) against golden reference
    uint32_t mask_key = 0x9a8b7c6d;
    const uint8_t* k_bytes = reinterpret_cast<const uint8_t*>(&mask_key);
    for (size_t size = 0; size <= 135; ++size) {
        std::string original(size, '\0');
        for (size_t j = 0; j < size; ++j) original[j] = static_cast<char>((j * 31 + 7) & 0xFF);
        std::string test_buf = original;
        unmask_payload_inplace(reinterpret_cast<uint8_t*>(test_buf.data()), test_buf.size(), mask_key);

        // Verify against golden scalar reference
        for (size_t j = 0; j < size; ++j) {
            uint8_t expected = static_cast<uint8_t>(original[j]) ^ k_bytes[j % 4];
            assert(static_cast<uint8_t>(test_buf[j]) == expected);
        }

        // Unmasking again with same key must restore original
        unmask_payload_inplace(reinterpret_cast<uint8_t*>(test_buf.data()), test_buf.size(), mask_key);
        assert(test_buf == original);
    }

    std::cout << "  -> Passed (Tiers 1-5 validated across all lengths 0-135 bytes)\n";
}

// 3. Test In-Memory Mock Transport to verify WebSocketSession abstraction
class MockTransport : public IWebSocketTransport {
public:
    TransportProtocol protocol() const noexcept override { return TransportProtocol::Http1; }
    bool is_open() const noexcept override { return open_; }

    aegon::core::Task<int> write_frame(std::span<const uint8_t> header, std::string_view payload) override {
        written_frames_.append(reinterpret_cast<const char*>(header.data()), header.size());
        written_frames_.append(payload);
        co_return static_cast<int>(header.size() + payload.size());
    }

    aegon::core::Task<int> write_raw(std::string_view bytes) override {
        written_frames_.append(bytes);
        co_return static_cast<int>(bytes.size());
    }

    aegon::core::Task<void> close_transport() override {
        open_ = false;
        co_return;
    }

    bool open_{true};
    std::string written_frames_;
};

void test_websocket_session_mock() {
    std::cout << "[TEST 3] Testing WebSocketSession abstraction over MockTransport...\n";

    MockTransport mock;
    WebSocketSession session(mock, "/ws");

    // Feed a masked text frame "EchoMe!"
    // Len = 7. Mask key = 0x01020304
    uint8_t mask[4] = {0x01, 0x02, 0x03, 0x04};
    std::string payload = "EchoMe!";
    std::string masked_payload = payload;
    for (size_t i = 0; i < masked_payload.size(); ++i) {
        masked_payload[i] ^= mask[i % 4];
    }

    std::string wire_frame;
    wire_frame.push_back(static_cast<char>(0x81)); // FIN + Text
    wire_frame.push_back(static_cast<char>(0x80 | 7)); // Masked + 7
    wire_frame.append(reinterpret_cast<const char*>(mask), 4);
    wire_frame.append(masked_payload);

    auto task = session.process_incoming_data(wire_frame);
    task.resume();

    // Verify default echo produced unmasked server frame
    assert(!mock.written_frames_.empty());
    FrameHeader resp_hdr{};
    auto parse_res = parse_frame_header(mock.written_frames_, resp_hdr);
    assert(parse_res == FrameParseResult::Complete);
    assert(resp_hdr.opcode == Opcode::Text);
    assert(!resp_hdr.masked);
    assert(resp_hdr.payload_len == 7);
    std::string_view echoed = mock.written_frames_.substr(resp_hdr.header_len, resp_hdr.payload_len);
    assert(echoed == "EchoMe!");

    std::cout << "  -> Passed: MockTransport session echoed '" << echoed << "'\n";
}

// 4. Test Live End-to-End WebSocket Server over loopback socket
void test_live_websocket_server() {
    std::cout << "[TEST 4] Testing Live WebSocket Server on TCP loopback port 19890...\n";

    uint16_t port = 19890;
    Server server;
    // Register echo route
    server.ws("/ws");
    server.listen(port, "127.0.0.1");

    std::thread server_thread([&]() {
        server.run();
    });
    server_thread.detach();

    // Wait for server to bind
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Connect raw TCP socket
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int conn_res = -1;
    for (int retry = 0; retry < 10; ++retry) {
        conn_res = ::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (conn_res == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(conn_res == 0);

    // 1. Upgrade request
    std::string upgrade_req =
        "GET /ws HTTP/1.1\r\n"
        "Host: 127.0.0.1:19890\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    ssize_t sent = ::send(sock, upgrade_req.data(), upgrade_req.size(), 0);
    assert(sent == static_cast<ssize_t>(upgrade_req.size()));

    char buf[4096]{};
    ssize_t recvd = ::recv(sock, buf, sizeof(buf) - 1, 0);
    assert(recvd > 0);
    std::string_view resp(buf, static_cast<size_t>(recvd));
    assert(resp.find("101 Switching Protocols") != std::string_view::npos);
    assert(resp.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string_view::npos);
    std::cout << "  -> Handshake 101 OK\n";

    // 2. Send masked Text Frame: "Hello Aegon WS!"
    std::string msg = "Hello Aegon WS!";
    uint8_t mask[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    std::string frame;
    frame.push_back(static_cast<char>(0x81)); // FIN + Text
    frame.push_back(static_cast<char>(0x80 | msg.size())); // Masked + len
    frame.append(reinterpret_cast<const char*>(mask), 4);
    for (size_t i = 0; i < msg.size(); ++i) {
        frame.push_back(static_cast<char>(msg[i] ^ mask[i % 4]));
    }

    sent = ::send(sock, frame.data(), frame.size(), 0);
    assert(sent == static_cast<ssize_t>(frame.size()));

    recvd = ::recv(sock, buf, sizeof(buf), 0);
    assert(recvd > 0);
    FrameHeader echoed_hdr{};
    std::string_view echoed_sv(buf, static_cast<size_t>(recvd));
    auto res = parse_frame_header(echoed_sv, echoed_hdr);
    assert(res == FrameParseResult::Complete);
    assert(echoed_hdr.opcode == Opcode::Text);
    assert(echoed_hdr.payload_len == msg.size());
    std::string_view echoed_payload = echoed_sv.substr(echoed_hdr.header_len, echoed_hdr.payload_len);
    assert(echoed_payload == msg);
    std::cout << "  -> Echo text frame OK: '" << echoed_payload << "'\n";

    // 3. Send Close Frame
    std::string close_frame;
    close_frame.push_back(static_cast<char>(0x88)); // FIN + Close
    close_frame.push_back(static_cast<char>(0x80 | 2)); // Masked + 2 bytes code
    close_frame.append(reinterpret_cast<const char*>(mask), 4);
    uint16_t be_code = htobe16(1000);
    const auto* code_bytes = reinterpret_cast<const uint8_t*>(&be_code);
    close_frame.push_back(static_cast<char>(code_bytes[0] ^ mask[0]));
    close_frame.push_back(static_cast<char>(code_bytes[1] ^ mask[1]));

    sent = ::send(sock, close_frame.data(), close_frame.size(), 0);
    assert(sent == static_cast<ssize_t>(close_frame.size()));

    recvd = ::recv(sock, buf, sizeof(buf), 0);
    if (recvd > 0) {
        FrameHeader close_resp{};
        parse_frame_header(std::string_view(buf, static_cast<size_t>(recvd)), close_resp);
        assert(close_resp.opcode == Opcode::Close);
    }
    ::close(sock);
    std::cout << "  -> Clean Close OK\n";

    // 4. Test bad non-upgrade GET to /ws returns 426
    int sock2 = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(::connect(sock2, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    std::string plain_get =
        "GET /ws HTTP/1.1\r\n"
        "Host: 127.0.0.1:19890\r\n"
        "Connection: close\r\n\r\n";
    ::send(sock2, plain_get.data(), plain_get.size(), 0);
    recvd = ::recv(sock2, buf, sizeof(buf) - 1, 0);
    assert(recvd > 0);
    buf[recvd] = '\0';
    std::string_view plain_resp(buf, static_cast<size_t>(recvd));
    assert(plain_resp.find("426 Upgrade Required") != std::string_view::npos);
    ::close(sock2);
    std::cout << "  -> Reject non-upgrade GET /ws (426 Upgrade Required) OK\n";

    server.stop();
}

int main() {
    std::cout << "=== Running Aegon WebSocket Tests ===\n";
    test_handshake_rfc_vector();
    test_frame_header_parsing_and_serializing();
    test_websocket_session_mock();
    test_live_websocket_server();
    std::cout << "=== All WebSocket Tests Passed Successfully! ===\n";
    return 0;
}
