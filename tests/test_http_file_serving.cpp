#include "http/Server.h"
#include "storage/File.h"
#include "storage/FileStream.h"
#include <iostream>
#include <fstream>
#include <cassert>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>

#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " __FILE__ ":" << __LINE__ << std::endl; \
            std::abort(); \
        } \
    } while (0)

using namespace aegon::http;

int main() {
    std::cout << "=======================================================\n";
    std::cout << "  AEGON HTTP FILE SERVING INTEGRATION TEST (H1/H2/H3)   \n";
    std::cout << "=======================================================\n\n";

    // Create a temporary test file with known patterns (128 KB)
    const std::string test_dir = "/tmp/aegon_http_test";
    std::filesystem::create_directories(test_dir);
    const std::string test_file = test_dir + "/sample.bin";
    
    constexpr size_t TEST_FILE_SIZE = 128 * 1024;
    std::string expected_data(TEST_FILE_SIZE, '\0');
    for (size_t i = 0; i < TEST_FILE_SIZE; ++i) {
        expected_data[i] = static_cast<char>((i * 31 + 17) & 0xFF);
    }
    {
        std::ofstream ofs(test_file, std::ios::binary);
        ofs.write(expected_data.data(), expected_data.size());
    }

    // 1. Test Plaintext HTTP/1.1 and HTTP/2 (h2c)
    {
        std::cout << "[Test 1] Testing Plaintext HTTP/1.1 and HTTP/2 (h2c) file serving...\n";
        uint16_t port = 19780;
        Router router;
        
        router.get("/file/path", [test_file](Context& ctx) {
            ctx.send_file(test_file, "application/octet-stream");
        });

        router.get("/file/handle", [test_file](Context& ctx) -> aegon::core::Task<void> {
            auto file = co_await aegon::storage::File::open(test_file, aegon::storage::OpenMode::ReadOnly);
            uint64_t sz = co_await file.size();
            ctx.send_file(std::move(file), sz, "application/octet-stream");
        });

        Server server(std::move(router));
        server.listen(port);
        std::thread server_thread([&server]() { server.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // HTTP/1.1 Plaintext path
        {
            std::string cmd = "curl -s http://127.0.0.1:" + std::to_string(port) + "/file/path";
            FILE* fp = popen(cmd.c_str(), "r");
            TEST_CHECK(fp != nullptr);
            std::string resp;
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
                resp.append(buf, n);
            }
            pclose(fp);
            TEST_CHECK(resp.size() == TEST_FILE_SIZE);
            TEST_CHECK(resp == expected_data);
            std::cout << "  -> PASS: HTTP/1.1 Plaintext file serving (path-based)\n";
        }

        // HTTP/1.1 Plaintext pre-opened File handle
        {
            std::string cmd = "curl -s http://127.0.0.1:" + std::to_string(port) + "/file/handle";
            FILE* fp = popen(cmd.c_str(), "r");
            TEST_CHECK(fp != nullptr);
            std::string resp;
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
                resp.append(buf, n);
            }
            pclose(fp);
            TEST_CHECK(resp.size() == TEST_FILE_SIZE);
            TEST_CHECK(resp == expected_data);
            std::cout << "  -> PASS: HTTP/1.1 Plaintext file serving (pre-opened File handle)\n";
        }

        // HTTP/2 Cleartext (h2c)
        {
            std::string cmd = "curl -s --http2-prior-knowledge http://127.0.0.1:" + std::to_string(port) + "/file/path";
            FILE* fp = popen(cmd.c_str(), "r");
            TEST_CHECK(fp != nullptr);
            std::string resp;
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
                resp.append(buf, n);
            }
            pclose(fp);
            TEST_CHECK(resp.size() == TEST_FILE_SIZE);
            TEST_CHECK(resp == expected_data);
            std::cout << "  -> PASS: HTTP/2 Cleartext (h2c) file serving\n";
        }

        server.stop();
        if (server_thread.joinable()) server_thread.join();
    }

    // 2. Test TLS: HTTP/1.1 TLS, HTTP/2 TLS, and HTTP/3 QUIC
    {
        std::cout << "\n[Test 2] Testing TLS: HTTP/1.1 TLS, HTTP/2 TLS, and HTTP/3 QUIC file serving...\n";
        uint16_t port = 19781;
        Router router;
        
        router.get("/file/path", [test_file](Context& ctx) {
            ctx.send_file(test_file, "application/octet-stream");
        });

        router.get("/file/handle", [test_file](Context& ctx) -> aegon::core::Task<void> {
            auto file = co_await aegon::storage::File::open(test_file, aegon::storage::OpenMode::ReadOnly);
            uint64_t sz = co_await file.size();
            ctx.send_file(std::move(file), sz, "application/octet-stream");
        });

        Server server(std::move(router));
        server.listen(port);
        server.enable_tls();
        server.enable_http3(true);

        std::thread server_thread([&server]() { server.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        // HTTP/1.1 TLS
        {
            std::string cmd = "curl -k -s --http1.1 https://127.0.0.1:" + std::to_string(port) + "/file/path";
            FILE* fp = popen(cmd.c_str(), "r");
            TEST_CHECK(fp != nullptr);
            std::string resp;
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
                resp.append(buf, n);
            }
            pclose(fp);
            TEST_CHECK(resp.size() == TEST_FILE_SIZE);
            TEST_CHECK(resp == expected_data);
            std::cout << "  -> PASS: HTTP/1.1 TLS file streaming via aegon_storage\n";
        }

        // HTTP/2 TLS
        {
            std::string cmd = "curl -k -s --http2 https://127.0.0.1:" + std::to_string(port) + "/file/path";
            FILE* fp = popen(cmd.c_str(), "r");
            TEST_CHECK(fp != nullptr);
            std::string resp;
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
                resp.append(buf, n);
            }
            pclose(fp);
            TEST_CHECK(resp.size() == TEST_FILE_SIZE);
            TEST_CHECK(resp == expected_data);
            std::cout << "  -> PASS: HTTP/2 TLS file streaming via aegon_storage\n";
        }

        // HTTP/3 QUIC
        {
            std::string cmd = "curl -k -s --http3-only https://127.0.0.1:" + std::to_string(port) + "/file/path";
            FILE* fp = popen(cmd.c_str(), "r");
            TEST_CHECK(fp != nullptr);
            std::string resp;
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
                resp.append(buf, n);
            }
            pclose(fp);
            TEST_CHECK(resp.size() == TEST_FILE_SIZE);
            TEST_CHECK(resp == expected_data);
            std::cout << "  -> PASS: HTTP/3 QUIC file streaming via aegon_storage\n";
        }

        server.stop();
        if (server_thread.joinable()) server_thread.join();
    }

    std::filesystem::remove_all(test_dir);
    std::cout << "\n=======================================================\n";
    std::cout << "ALL HTTP FILE SERVING TESTS (H1/H2/H3) PASSED!\n";
    std::cout << "=======================================================\n";
    return 0;
}

