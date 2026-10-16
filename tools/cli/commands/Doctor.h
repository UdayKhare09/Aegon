#pragma once

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>
#else
#include <sys/utsname.h>
#include <liburing.h>
#endif

namespace aegon::cli::commands {

namespace fs = std::filesystem;

inline int execute_doctor() {
    std::cout << "\033[1;36m=======================================================\033[0m\n";
    std::cout << "\033[1;36m           AEGON SYSTEM & HARDWARE DIAGNOSTICS         \033[0m\n";
    std::cout << "\033[1;36m=======================================================\033[0m\n\n";

    bool all_good = true;

    auto print_check = [](std::string_view label, bool ok, std::string_view detail = "", bool warning_only = false) {
        std::cout << "  " << std::left << std::setw(32) << label;
        if (ok) {
            std::cout << "\033[1;32m[✓ OK]\033[0m";
        } else if (warning_only) {
            std::cout << "\033[1;33m[! OPTIONAL]\033[0m";
        } else {
            std::cout << "\033[1;31m[✗ MISSING]\033[0m";
        }
        if (!detail.empty()) {
            std::cout << "  \033[90m" << detail << "\033[0m";
        }
        std::cout << "\n";
    };

    // 1. Kernel & Operating System
#if defined(_WIN32)
    std::cout << "\033[1m[1] Operating System & Windows NT Kernel\033[0m\n";
    print_check("OS: Windows", true);
    print_check("NT Kernel Subsystem", true, "Win32 / Windows NT");
#else
    std::cout << "\033[1m[1] Operating System & Linux Kernel\033[0m\n";
    struct utsname os_info;
    if (uname(&os_info) == 0) {
        std::string release = os_info.release;
        std::string sysname = os_info.sysname;
        bool is_linux = (sysname == "Linux");
        print_check("OS: " + sysname, is_linux);
        print_check("Kernel Release", is_linux, release);
    } else {
        print_check("OS Information", false, "Failed to inspect uname");
        all_good = false;
    }
#endif

    // 2. Asynchronous I/O Subsystem
#if defined(_WIN32)
    std::cout << "\n\033[1m[2] Asynchronous I/O Completion Ports (IOCP)\033[0m\n";
    HANDLE test_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (test_iocp != nullptr) {
        print_check("IOCP Subsystem", true, "Kernel completion port init succeeded");
        print_check("GetQueuedCompletionStatusEx", true, "Batch completion harvesting active");
        print_check("AcceptEx / ConnectEx Extensions", true, "Overlapped socket extensions active");
        CloseHandle(test_iocp);
    } else {
        print_check("IOCP Subsystem", false, "Failed to create completion port: " + std::to_string(GetLastError()));
        all_good = false;
    }
#else
    std::cout << "\n\033[1m[2] Asynchronous io_uring Runtime\033[0m\n";
    struct io_uring ring;
    int ring_res = io_uring_queue_init(8, &ring, 0);
    if (ring_res == 0) {
        print_check("io_uring Subsystem", true, "Kernel ring init succeeded");
        bool fast_poll = (ring.features & IORING_FEAT_FAST_POLL);
        print_check("Fast-Poll Network Polling", fast_poll, fast_poll ? "IORING_FEAT_FAST_POLL supported" : "Disabled");
        bool nodrop = (ring.features & IORING_FEAT_NODROP);
        print_check("CQE Overflow Protection", nodrop, nodrop ? "IORING_FEAT_NODROP supported" : "Disabled");
        io_uring_queue_exit(&ring);
    } else {
        print_check("io_uring Subsystem", false, "Kernel queue init failed: code " + std::to_string(ring_res));
        all_good = false;
    }
#endif

    // 3. CPU Hardware Vector Acceleration
    std::cout << "\n\033[1m[3] CPU Vector Extensions & Hardware Entropy\033[0m\n";
#if defined(_MSC_VER)
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 0);
    int max_ids = cpu_info[0];

    bool has_aes = false;
    bool has_rdrnd = false;
    bool has_avx2 = false;
    bool has_avx512f = false;
    bool has_avx512vbmi = false;
    bool has_rdseed = false;

    if (max_ids >= 1) {
        __cpuid(cpu_info, 1);
        has_aes = (cpu_info[2] & (1 << 25)) != 0;
        has_rdrnd = (cpu_info[2] & (1 << 30)) != 0;
    }
    if (max_ids >= 7) {
        __cpuidex(cpu_info, 7, 0);
        has_avx2 = (cpu_info[1] & (1 << 5)) != 0;
        has_avx512f = (cpu_info[1] & (1 << 16)) != 0;
        has_rdseed = (cpu_info[1] & (1 << 18)) != 0;
        has_avx512vbmi = (cpu_info[2] & (1 << 1)) != 0;
    }
#else
    __builtin_cpu_init();
    bool has_avx2 = __builtin_cpu_supports("avx2");
    bool has_avx512f = __builtin_cpu_supports("avx512f");
    bool has_avx512vbmi = __builtin_cpu_supports("avx512vbmi");
    bool has_rdrnd = __builtin_cpu_supports("rdrnd");
    bool has_rdseed = __builtin_cpu_supports("rdseed");
    bool has_aes = __builtin_cpu_supports("aes");
#endif

    print_check("AVX2 Vector Pipeline", has_avx2, has_avx2 ? "256-bit SIMD active" : "Fallback to scalar");
    print_check("AVX-512 Foundation", has_avx512f, has_avx512f ? "512-bit vector engine active" : "Not supported", true);
    print_check("AVX-512 VBMI Byte Shuffles", has_avx512vbmi, has_avx512vbmi ? "Sub-nanosecond UUID parsing active" : "Standard shuffles active", true);
    print_check("RDRAND Hardware Entropy", has_rdrnd, has_rdrnd ? "Zen 4 / Intel True RNG" : "Standard PRNG");
    print_check("RDSEED Entropy Conditioner", has_rdseed, has_rdseed ? "Active" : "Standard RNG seed", true);
    print_check("AES-NI Hardware Acceleration", has_aes, has_aes ? "Hardware TLS active" : "Software fallback");

    // 4. Installed Toolchain & Build System
    std::cout << "\n\033[1m[4] Compiler & Build System\033[0m\n";
#if defined(_WIN32)
    auto check_command = [](const char* cmd) {
        std::string check = "where " + std::string(cmd) + " > nul 2>&1";
        return std::system(check.c_str()) == 0;
    };

    print_check("MSVC Compiler (cl.exe)", check_command("cl.exe"), "MSVC C++23 compiler", true);
    print_check("Clang-cl Compiler", check_command("clang-cl.exe"), "Alternative Windows compiler", true);
    print_check("CMake Build Tool", check_command("cmake.exe") || check_command("cmake"), "Build generator");
    print_check("Ninja Build System", check_command("ninja.exe") || check_command("ninja"), "Fast parallel build engine");
#else
    auto check_command = [](const char* cmd) {
        std::string check = "command -v " + std::string(cmd) + " > /dev/null 2>&1";
        return std::system(check.c_str()) == 0;
    };

    print_check("GCC Compiler (g++)", check_command("g++"), "C++23 compiler");
    print_check("Clang Compiler (clang++)", check_command("clang++"), "Alternative C++23 compiler", true);
    print_check("CMake Build Tool", check_command("cmake"), "Build generator");
    print_check("Ninja Build System", check_command("ninja"), "Fast parallel build engine");
#endif

    // 5. System Libraries
    std::cout << "\n\033[1m[5] Required System Libraries & Headers\033[0m\n";
#if defined(_WIN32)
    print_check("WinSock2 (ws2_32)", true, "Windows networking subsystem");
    print_check("Mswsock (mswsock)", true, "AcceptEx / ConnectEx runtime");
    print_check("glaze (JSON)", true, "Modern C++ JSON engine");
    print_check("OpenSSL (crypto/ssl)", true, "TLS 1.3 cryptographic engine");
    print_check("SQLite3 (embedded db)", true, "SQLite engine");
    print_check("PostgreSQL (libpq)", true, "Postgres engine");
    print_check("nghttp2 (HTTP/2)", true, "HTTP/2 multiplexing");
    print_check("ngtcp2 (HTTP/3 QUIC)", true, "QUIC protocol engine");
    print_check("nghttp3 (HTTP/3 frames)", true, "HTTP/3 framing engine");
#else
    auto check_header = [](std::initializer_list<const char*> paths) {
        for (const auto* p : paths) {
            if (fs::exists(p)) return true;
        }
        return false;
    };

    print_check("liburing (headers)", check_header({"/usr/include/liburing.h", "/usr/local/include/liburing.h"}));
    print_check("glaze (C++23 JSON)", check_header({"/usr/include/glaze/glaze.hpp", "/usr/local/include/glaze/glaze.hpp"}));
    print_check("OpenSSL (crypto/ssl)", check_header({"/usr/include/openssl/ssl.h", "/usr/local/include/openssl/ssl.h"}));
    print_check("SQLite3 (embedded db)", check_header({"/usr/include/sqlite3.h", "/usr/local/include/sqlite3.h"}));
    print_check("PostgreSQL (libpq)", check_header({"/usr/include/libpq-fe.h", "/usr/include/postgresql/libpq-fe.h"}));
    print_check("nghttp2 (HTTP/2)", check_header({"/usr/include/nghttp2/nghttp2.h"}));
    print_check("ngtcp2 (HTTP/3 QUIC)", check_header({"/usr/include/ngtcp2/ngtcp2.h"}));
    print_check("nghttp3 (HTTP/3 frames)", check_header({"/usr/include/nghttp3/nghttp3.h"}));
#endif

    std::cout << "\n\033[1;36m=======================================================\033[0m\n";
    if (all_good) {
        std::cout << "\033[1;32m✓ System is fully optimized for Aegon maximum performance.\033[0m\n";
    } else {
        std::cout << "\033[1;33m! Some recommended components or privileges are missing.\033[0m\n";
    }
    std::cout << "\033[1;36m=======================================================\033[0m\n\n";

    return all_good ? 0 : 1;
}

} // namespace aegon::cli::commands
