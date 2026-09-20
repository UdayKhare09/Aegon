#pragma once

#include "Version.h"
#include <iostream>
#include <string_view>

namespace aegon::cli::commands {

inline int execute_version() {
    std::cout << "\033[1;36m";
    std::cout << R"banner(
    ___                               
   /   |  ___  ____ _____  ____       
  / /| | / _ \/ __ `/ __ \/ __ \
 / ___ |/  __/ /_/ / /_/ / / / /      
/_/  |_|\___/\__, /\____/_/ /_/       
            /____/                    
)banner" << "\033[0m\n";

    std::cout << "\033[1mAegon C++26 Asynchronous Web & Data Framework\033[0m\n";
    std::cout << "  Version          : \033[1;32m" << aegon::version.string << "\033[0m\n";
    std::cout << "  Standard         : \033[33mC++26\033[0m\n";

#if defined(__GNUC__)
    std::cout << "  Compiler         : GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << "\n";
#elif defined(__clang__)
    std::cout << "  Compiler         : Clang " << __clang_version__ << "\n";
#endif

#if defined(__x86_64__) || defined(_M_X64)
    std::cout << "  Architecture     : x86_64\n";
#if defined(__AVX512F__)
    std::cout << "  SIMD Pipeline    : AVX-512 (512-bit registers active)\n";
#elif defined(__AVX2__)
    std::cout << "  SIMD Pipeline    : AVX2 (256-bit registers active)\n";
#else
    std::cout << "  SIMD Pipeline    : Portable Scalar\n";
#endif
#endif

    std::cout << "  License          : Apache-2.0\n";
    std::cout << "  Notice           : AI-written, human-directed framework. Early alpha releases may have breaking changes.\n\n";

    return 0;
}

} // namespace aegon::cli::commands
