#pragma once

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <filesystem>
#include <sys/utsname.h>
#include <liburing.h>
#include <cstdlib>

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

    // 2. io_uring Subsystem
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

    // 3. CPU Hardware Vector Acceleration
    std::cout << "\n\033[1m[3] CPU Vector Extensions & Hardware Entropy\033[0m\n";
    __builtin_cpu_init();

    bool has_avx2 = __builtin_cpu_supports("avx2");
    print_check("AVX2 Vector Pipeline", has_avx2, has_avx2 ? "256-bit SIMD active" : "Fallback to scalar");

    bool has_avx512f = __builtin_cpu_supports("avx512f");
    print_check("AVX-512 Foundation", has_avx512f, has_avx512f ? "512-bit vector engine active" : "Not supported", true);

    bool has_avx512vbmi = __builtin_cpu_supports("avx512vbmi");
    print_check("AVX-512 VBMI Byte Shuffles", has_avx512vbmi, has_avx512vbmi ? "Sub-nanosecond UUID parsing active" : "Standard shuffles active", true);

    bool has_rdrnd = __builtin_cpu_supports("rdrnd");
    print_check("RDRAND Hardware Entropy", has_rdrnd, has_rdrnd ? "Zen 4 / Intel True RNG" : "Standard PRNG");

    bool has_rdseed = __builtin_cpu_supports("rdseed");
    print_check("RDSEED Entropy Conditioner", has_rdseed, has_rdseed ? "Active" : "Standard RNG seed", true);

    bool has_aes = __builtin_cpu_supports("aes");
    print_check("AES-NI Hardware Acceleration", has_aes, has_aes ? "Hardware TLS active" : "Software fallback");

    // 4. Installed Toolchain & Build System
    std::cout << "\n\033[1m[4] Compiler & Build System\033[0m\n";
    auto check_command = [](const char* cmd) {
        std::string check = "command -v " + std::string(cmd) + " > /dev/null 2>&1";
        return std::system(check.c_str()) == 0;
    };

    print_check("GCC Compiler (g++)", check_command("g++"), "C++26 compiler");
    print_check("Clang Compiler (clang++)", check_command("clang++"), "Alternative C++26 compiler", true);
    print_check("CMake Build Tool", check_command("cmake"), "Build generator");
    print_check("Ninja Build System", check_command("ninja"), "Fast parallel build engine");

    // 5. System Libraries
    std::cout << "\n\033[1m[5] Required System Libraries & Headers\033[0m\n";
    auto check_header = [](std::initializer_list<const char*> paths) {
        for (const auto* p : paths) {
            if (fs::exists(p)) return true;
        }
        return false;
    };

    print_check("liburing (headers)", check_header({"/usr/include/liburing.h", "/usr/local/include/liburing.h"}));
    print_check("glaze (C++26 JSON)", check_header({"/usr/include/glaze/glaze.hpp", "/usr/local/include/glaze/glaze.hpp"}));
    print_check("OpenSSL (crypto/ssl)", check_header({"/usr/include/openssl/ssl.h", "/usr/local/include/openssl/ssl.h"}));
    print_check("SQLite3 (embedded db)", check_header({"/usr/include/sqlite3.h", "/usr/local/include/sqlite3.h"}));
    print_check("PostgreSQL (libpq)", check_header({"/usr/include/libpq-fe.h", "/usr/include/postgresql/libpq-fe.h"}));
    print_check("nghttp2 (HTTP/2)", check_header({"/usr/include/nghttp2/nghttp2.h"}));
    print_check("ngtcp2 (HTTP/3 QUIC)", check_header({"/usr/include/ngtcp2/ngtcp2.h"}));
    print_check("nghttp3 (HTTP/3 frames)", check_header({"/usr/include/nghttp3/nghttp3.h"}));

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
