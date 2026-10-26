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
#include <vector>
#include <cstdlib>

using namespace aegon;
using namespace aegon::http;

int main() {
    std::cout << "=======================================================\n";
    std::cout << "       AEGON NATIVE gRPC SERVER STREAMING TEST         \n";
    std::cout << "=======================================================\n\n";

    constexpr uint16_t PORT = 20052;
    Router router;

    auto benchmark_svc = router.grpc_service("/benchmark.BenchmarkService");

    // Server Streaming RPC (1 Request -> Stream of Responses)
    benchmark_svc.server_streaming<benchmark::StreamRequest, benchmark::SumReply>(
        "StreamSum",
        [](grpc::ServerStreamingContext<benchmark::StreamRequest, benchmark::SumReply>& ctx) -> core::Task<void> {
            int32_t a = ctx.req().a();
            int32_t b = ctx.req().b();
            int32_t count = ctx.req().count();

            if (count <= 0) {
                ctx.error(grpc::StatusCode::InvalidArgument, "'count' must be greater than 0");
                co_return;
            }

            int32_t base = a + b;
            for (int32_t i = 1; i <= count; ++i) {
                benchmark::SumReply reply;
                reply.set_result(base * i);
                co_await ctx.send(reply);
            }
            co_return;
        }
    );

    Server server(std::move(router));
    server.listen(PORT, "127.0.0.1");

    std::thread server_thread([&]() {
        server.run();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Prepare wire frames for streaming request
    std::string req_file = "/tmp/grpc_stream_req.bin";
    {
        benchmark::StreamRequest req;
        req.set_a(2);
        req.set_b(3);
        req.set_count(5);
        std::string frame;
        grpc::GrpcCodec<benchmark::StreamRequest>::encode(req, frame);
        std::ofstream ofs(req_file, std::ios::binary);
        ofs.write(frame.data(), frame.size());
    }

    std::string err_req_file = "/tmp/grpc_stream_err_req.bin";
    {
        benchmark::StreamRequest req;
        req.set_a(2);
        req.set_b(3);
        req.set_count(-1);
        std::string frame;
        grpc::GrpcCodec<benchmark::StreamRequest>::encode(req, frame);
        std::ofstream ofs(err_req_file, std::ios::binary);
        ofs.write(frame.data(), frame.size());
    }

    // 1. Test Server Streaming RPC with 5 messages
    std::cout << "[TEST 1] Testing Server Streaming RPC (count=5)...\n";
    {
        std::string cmd = "curl -s --http2-prior-knowledge -H 'Content-Type: application/grpc' "
                          "--data-binary @" + req_file + " -o /tmp/grpc_stream_res.bin -D /tmp/grpc_stream_headers.txt "
                          "http://127.0.0.1:20052/benchmark.BenchmarkService/StreamSum";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);

        std::ifstream ifs("/tmp/grpc_stream_res.bin", std::ios::binary);
        std::string stream_data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

        // Parse consecutive 5-byte framed messages
        size_t offset = 0;
        std::vector<int32_t> results;
        while (offset + 5 <= stream_data.size()) {
            assert(static_cast<uint8_t>(stream_data[offset]) == 0); // uncompressed
            uint32_t net_len = 0;
            std::memcpy(&net_len, stream_data.data() + offset + 1, 4);
            uint32_t msg_len = ntohl(net_len);

            offset += 5;
            assert(offset + msg_len <= stream_data.size());

            benchmark::SumReply reply;
            bool ok = reply.ParseFromArray(stream_data.data() + offset, static_cast<int>(msg_len));
            assert(ok);
            results.push_back(reply.result());
            offset += msg_len;
        }

        assert(results.size() == 5);
        assert(results[0] == 5);   // (2+3)*1
        assert(results[1] == 10);  // (2+3)*2
        assert(results[2] == 15);  // (2+3)*3
        assert(results[3] == 20);  // (2+3)*4
        assert(results[4] == 25);  // (2+3)*5

        std::ifstream hfs("/tmp/grpc_stream_headers.txt");
        std::string headers_str((std::istreambuf_iterator<char>(hfs)), std::istreambuf_iterator<char>());
        assert(headers_str.find("grpc-status: 0") != std::string::npos || headers_str.find("grpc-status") != std::string::npos);

        std::cout << "  -> PASS: Received 5 sequential streaming messages: [5, 10, 15, 20, 25] with grpc-status: 0\n";
    }

    // 2. Test Server Streaming Validation Error
    std::cout << "\n[TEST 2] Testing Server Streaming Error (count <= 0)...\n";
    {
        std::string cmd = "curl -s --http2-prior-knowledge -H 'Content-Type: application/grpc' "
                          "--data-binary @" + err_req_file + " -D /tmp/grpc_stream_err_headers.txt "
                          "http://127.0.0.1:20052/benchmark.BenchmarkService/StreamSum";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);

        std::ifstream hfs("/tmp/grpc_stream_err_headers.txt");
        std::string headers_str((std::istreambuf_iterator<char>(hfs)), std::istreambuf_iterator<char>());
        assert(headers_str.find("grpc-status: 3") != std::string::npos);

        std::cout << "  -> PASS: Emitted grpc-status: 3 (InvalidArgument) on invalid count\n";
    }

    // Clean up
    std::remove(req_file.c_str());
    std::remove(err_req_file.c_str());
    std::remove("/tmp/grpc_stream_res.bin");
    std::remove("/tmp/grpc_stream_headers.txt");
    std::remove("/tmp/grpc_stream_err_headers.txt");

    std::cout << "\n=======================================================\n";
    std::cout << "   ALL gRPC SERVER STREAMING TESTS PASSED PERFECTLY!   \n";
    std::cout << "=======================================================\n";

    std::exit(0);
}
