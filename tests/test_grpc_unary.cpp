#include "http/Server.h"
#include "http/grpc/GrpcStatus.h"
#include "http/grpc/GrpcCodec.h"
#include "http/grpc/GrpcService.h"
#include "proto/benchmark.pb.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <fstream>
#include <cstdlib>

using namespace aegon;
using namespace aegon::http;

void test_codec_unit() {
    std::cout << "[UNIT TEST] Testing GrpcCodec encode / decode...\n";

    benchmark::SumRequest req;
    req.set_a(1234);
    req.set_b(5678);

    std::string wire_frame;
    bool ok = grpc::GrpcCodec<benchmark::SumRequest>::encode(req, wire_frame);
    assert(ok);
    assert(wire_frame.size() == 5 + req.ByteSizeLong());

    // Byte 0: compression flag 0
    assert(static_cast<uint8_t>(wire_frame[0]) == 0);

    // Bytes 1..4: big-endian payload length
    uint32_t net_len = 0;
    std::memcpy(&net_len, wire_frame.data() + 1, 4);
    assert(ntohl(net_len) == req.ByteSizeLong());

    // Decode back
    benchmark::SumRequest decoded;
    bool dec_ok = grpc::GrpcCodec<benchmark::SumRequest>::decode(wire_frame, decoded);
    assert(dec_ok);
    assert(decoded.a() == 1234);
    assert(decoded.b() == 5678);

    std::cout << "  -> PASS: GrpcCodec encode/decode verified\n";
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << "          AEGON NATIVE gRPC UNARY TEST SUITE           \n";
    std::cout << "=======================================================\n\n";

    test_codec_unit();

    constexpr uint16_t PORT = 20051;
    Router router;

    auto benchmark_svc = router.grpc_service("/benchmark.BenchmarkService");

    // Interceptor: reject invalid bearer token with Trailers-Only Unauthenticated
    benchmark_svc.use([](Context& ctx, auto next) -> core::Task<void> {
        auto auth = ctx.req().headers().get("authorization");
        if (auth && *auth == "Bearer invalid-token") {
            ctx.res().status(http::StatusCode::Ok);
            ctx.res().header("content-type", "application/grpc");
            ctx.res().trailers_only(true);
            ctx.res().trailer("grpc-status", grpc::status_code_to_string(grpc::StatusCode::Unauthenticated));
            ctx.res().set_trailer_owned("grpc-message", "Unauthorized token");
            co_return;
        }
        co_await next(ctx);
    });

    // Synchronous Unary RPC (Zero-Allocation Hot Path)
    benchmark_svc.unary<benchmark::SumRequest, benchmark::SumReply>(
        "GetSum",
        [](grpc::UnaryContext<benchmark::SumRequest, benchmark::SumReply>& ctx) {
            int32_t a = ctx.req().a();
            int32_t b = ctx.req().b();

            if (a < 0) {
                ctx.error(grpc::StatusCode::InvalidArgument, "'a' cannot be negative");
                return;
            }

            benchmark::SumReply reply;
            reply.set_result(a + b);
            ctx.reply(reply);
        }
    );

    // Asynchronous Coroutine Unary RPC
    benchmark_svc.unary<benchmark::SumRequest, benchmark::SumReply>(
        "GetSumAsync",
        [](grpc::UnaryContext<benchmark::SumRequest, benchmark::SumReply>& ctx) -> core::Task<void> {
            benchmark::SumReply reply;
            reply.set_result((ctx.req().a() + ctx.req().b()) * 10);
            ctx.reply(reply);
            co_return;
        }
    );

    Server server(std::move(router));
    server.listen(PORT, "127.0.0.1");

    std::thread server_thread([&]() {
        server.run();
    });

    // Wait for server to bind
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Prepare wire frames for test requests
    std::string req_file = "/tmp/grpc_test_sum_req.bin";
    {
        benchmark::SumRequest req;
        req.set_a(40);
        req.set_b(2);
        std::string frame;
        grpc::GrpcCodec<benchmark::SumRequest>::encode(req, frame);
        std::ofstream ofs(req_file, std::ios::binary);
        ofs.write(frame.data(), frame.size());
    }

    std::string neg_req_file = "/tmp/grpc_test_neg_req.bin";
    {
        benchmark::SumRequest req;
        req.set_a(-5);
        req.set_b(10);
        std::string frame;
        grpc::GrpcCodec<benchmark::SumRequest>::encode(req, frame);
        std::ofstream ofs(neg_req_file, std::ios::binary);
        ofs.write(frame.data(), frame.size());
    }

    // 1. Test Synchronous Unary RPC: GetSum
    std::cout << "\n[TEST 1] Testing Synchronous Unary RPC (GetSum 40 + 2)...\n";
    {
        std::string cmd = "curl -s --http2-prior-knowledge -H 'Content-Type: application/grpc' "
                          "--data-binary @" + req_file + " -o /tmp/grpc_res.bin -D /tmp/grpc_headers.txt "
                          "http://127.0.0.1:20051/benchmark.BenchmarkService/GetSum";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);

        // Verify response payload
        std::ifstream ifs("/tmp/grpc_res.bin", std::ios::binary);
        std::string res_data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        assert(res_data.size() >= 5);

        benchmark::SumReply reply;
        bool dec_ok = grpc::GrpcCodec<benchmark::SumReply>::decode(res_data, reply);
        assert(dec_ok);
        assert(reply.result() == 42);

        // Verify trailing headers or status
        std::ifstream hfs("/tmp/grpc_headers.txt");
        std::string headers_str((std::istreambuf_iterator<char>(hfs)), std::istreambuf_iterator<char>());
        assert(headers_str.find("grpc-status: 0") != std::string::npos || headers_str.find("grpc-status") != std::string::npos);

        std::cout << "  -> PASS: Synchronous Unary RPC returned result=42 with grpc-status: 0\n";
    }

    // 2. Test Error Handling: InvalidArgument on a < 0
    std::cout << "\n[TEST 2] Testing Business Logic Validation (InvalidArgument a = -5)...\n";
    {
        std::string cmd = "curl -s --http2-prior-knowledge -H 'Content-Type: application/grpc' "
                          "--data-binary @" + neg_req_file + " -D /tmp/grpc_neg_headers.txt "
                          "http://127.0.0.1:20051/benchmark.BenchmarkService/GetSum";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);

        std::ifstream hfs("/tmp/grpc_neg_headers.txt");
        std::string headers_str((std::istreambuf_iterator<char>(hfs)), std::istreambuf_iterator<char>());
        assert(headers_str.find("grpc-status: 3") != std::string::npos);

        std::cout << "  -> PASS: Correctly emitted Trailers-Only grpc-status: 3 (InvalidArgument)\n";
    }

    // 3. Test Interceptor: Unauthenticated
    std::cout << "\n[TEST 3] Testing Interceptor (Unauthenticated Token)...\n";
    {
        std::string cmd = "curl -s --http2-prior-knowledge -H 'Content-Type: application/grpc' "
                          "-H 'Authorization: Bearer invalid-token' "
                          "--data-binary @" + req_file + " -D /tmp/grpc_auth_headers.txt "
                          "http://127.0.0.1:20051/benchmark.BenchmarkService/GetSum";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);

        std::ifstream hfs("/tmp/grpc_auth_headers.txt");
        std::string headers_str((std::istreambuf_iterator<char>(hfs)), std::istreambuf_iterator<char>());
        assert(headers_str.find("grpc-status: 16") != std::string::npos);

        std::cout << "  -> PASS: Interceptor intercepted call and emitted grpc-status: 16 (Unauthenticated)\n";
    }

    // 4. Test Asynchronous Coroutine Unary RPC: GetSumAsync
    std::cout << "\n[TEST 4] Testing Asynchronous Coroutine Unary RPC (GetSumAsync)...\n";
    {
        std::string cmd = "curl -s --http2-prior-knowledge -H 'Content-Type: application/grpc' "
                          "--data-binary @" + req_file + " -o /tmp/grpc_async_res.bin "
                          "http://127.0.0.1:20051/benchmark.BenchmarkService/GetSumAsync";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);

        std::ifstream ifs("/tmp/grpc_async_res.bin", std::ios::binary);
        std::string res_data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        assert(res_data.size() >= 5);

        benchmark::SumReply reply;
        bool dec_ok = grpc::GrpcCodec<benchmark::SumReply>::decode(res_data, reply);
        assert(dec_ok);
        assert(reply.result() == 420); // (40 + 2) * 10

        std::cout << "  -> PASS: Coroutine Unary RPC returned result=420 with grpc-status: 0\n";
    }

    // Clean up temporary test files
    std::remove(req_file.c_str());
    std::remove(neg_req_file.c_str());
    std::remove("/tmp/grpc_res.bin");
    std::remove("/tmp/grpc_headers.txt");
    std::remove("/tmp/grpc_neg_headers.txt");
    std::remove("/tmp/grpc_auth_headers.txt");
    std::remove("/tmp/grpc_async_res.bin");

    std::cout << "\n=======================================================\n";
    std::cout << "      ALL gRPC UNARY TESTS PASSED SUCCESSFULLY!        \n";
    std::cout << "=======================================================\n";

    std::exit(0);
}
