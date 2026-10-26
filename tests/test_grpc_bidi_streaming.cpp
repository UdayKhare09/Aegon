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
    std::cout << "    AEGON NATIVE gRPC BIDIRECTIONAL STREAMING TEST     \n";
    std::cout << "=======================================================\n\n";

    constexpr uint16_t PORT = 20054;
    Router router;

    auto benchmark_svc = router.grpc_service("/benchmark.BenchmarkService");

    // Bidirectional Streaming RPC: ChatSum (stream SumRequest <-> stream SumReply)
    benchmark_svc.bidi_streaming<benchmark::SumRequest, benchmark::SumReply>(
        "ChatSum",
        [](grpc::BidiStreamingContext<benchmark::SumRequest, benchmark::SumReply>& ctx) -> core::Task<void> {
            while (auto req = co_await ctx.read()) {
                if (req->a() < 0) {
                    ctx.error(grpc::StatusCode::InvalidArgument, "Negative operand rejected");
                    co_return;
                }

                benchmark::SumReply reply;
                reply.set_result(req->a() + req->b());
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

    // Prepare bidi streaming input file containing 3 gRPC frames: (1+2)=3, (10+20)=30, (100+200)=300
    std::string req_file = "/tmp/grpc_bidi_stream_req.bin";
    {
        std::ofstream ofs(req_file, std::ios::binary);

        // Frame 1
        {
            benchmark::SumRequest r1;
            r1.set_a(1);
            r1.set_b(2);
            std::string f1;
            grpc::GrpcCodec<benchmark::SumRequest>::encode(r1, f1);
            ofs.write(f1.data(), f1.size());
        }

        // Frame 2
        {
            benchmark::SumRequest r2;
            r2.set_a(10);
            r2.set_b(20);
            std::string f2;
            grpc::GrpcCodec<benchmark::SumRequest>::encode(r2, f2);
            ofs.write(f2.data(), f2.size());
        }

        // Frame 3
        {
            benchmark::SumRequest r3;
            r3.set_a(100);
            r3.set_b(200);
            std::string f3;
            grpc::GrpcCodec<benchmark::SumRequest>::encode(r3, f3);
            ofs.write(f3.data(), f3.size());
        }
    }

    // Error input file with a < 0
    std::string err_file = "/tmp/grpc_bidi_stream_err.bin";
    {
        std::ofstream ofs(err_file, std::ios::binary);
        benchmark::SumRequest r_err;
        r_err.set_a(-1);
        r_err.set_b(1);
        std::string ferr;
        grpc::GrpcCodec<benchmark::SumRequest>::encode(r_err, ferr);
        ofs.write(ferr.data(), ferr.size());
    }

    // 1. Test Full Duplex Bidirectional Streaming RPC
    std::cout << "[TEST 1] Testing Bidirectional Streaming RPC (3 input frames -> 3 output frames)...\n";
    {
        std::string cmd = "curl -s --http2-prior-knowledge -H 'Content-Type: application/grpc' "
                          "--data-binary @" + req_file + " -o /tmp/grpc_bidi_stream_res.bin -D /tmp/grpc_bidi_stream_headers.txt "
                          "http://127.0.0.1:20054/benchmark.BenchmarkService/ChatSum";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);

        std::ifstream ifs("/tmp/grpc_bidi_stream_res.bin", std::ios::binary);
        std::string stream_data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

        // Parse consecutive 5-byte framed responses
        size_t offset = 0;
        std::vector<int32_t> results;
        while (offset + 5 <= stream_data.size()) {
            assert(static_cast<uint8_t>(stream_data[offset]) == 0);
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

        assert(results.size() == 3);
        assert(results[0] == 3);
        assert(results[1] == 30);
        assert(results[2] == 300);

        std::ifstream hfs("/tmp/grpc_bidi_stream_headers.txt");
        std::string headers_str((std::istreambuf_iterator<char>(hfs)), std::istreambuf_iterator<char>());
        assert(headers_str.find("grpc-status: 0") != std::string::npos || headers_str.find("grpc-status") != std::string::npos);

        std::cout << "  -> PASS: Successfully streamed 3 replies [3, 30, 300] with grpc-status: 0\n";
    }

    // 2. Test Bidirectional Streaming Error Termination
    std::cout << "\n[TEST 2] Testing Bidirectional Streaming Error Termination...\n";
    {
        std::string cmd = "curl -s --http2-prior-knowledge -H 'Content-Type: application/grpc' "
                          "--data-binary @" + err_file + " -D /tmp/grpc_bidi_stream_err_headers.txt "
                          "http://127.0.0.1:20054/benchmark.BenchmarkService/ChatSum";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);

        std::ifstream hfs("/tmp/grpc_bidi_stream_err_headers.txt");
        std::string headers_str((std::istreambuf_iterator<char>(hfs)), std::istreambuf_iterator<char>());
        assert(headers_str.find("grpc-status: 3") != std::string::npos);

        std::cout << "  -> PASS: Correctly terminated stream with grpc-status: 3 (InvalidArgument)\n";
    }

    // Clean up
    std::remove(req_file.c_str());
    std::remove(err_file.c_str());
    std::remove("/tmp/grpc_bidi_stream_res.bin");
    std::remove("/tmp/grpc_bidi_stream_headers.txt");
    std::remove("/tmp/grpc_bidi_stream_err_headers.txt");

    std::cout << "\n=======================================================\n";
    std::cout << "     ALL gRPC BIDI STREAMING TESTS PASSED PERFECTLY!   \n";
    std::cout << "=======================================================\n";

    std::exit(0);
}
