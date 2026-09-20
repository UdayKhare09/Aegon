#pragma once

#include <iostream>
#include <filesystem>
#include <string>
#include <cstdlib>

namespace aegon::cli::commands {

namespace fs = std::filesystem;

inline int execute_build() {
    if (!fs::exists("CMakeLists.txt")) {
        std::cerr << "\033[1;31mError:\033[0m No CMakeLists.txt found in current directory (" << fs::current_path().string() << ").\n";
        return 1;
    }

    std::cout << "\033[1;36m==>\033[0m Configuring build with Ninja...\n";
    int cfg_res = std::system("cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release");
    if (cfg_res != 0) {
        std::cerr << "\033[1;31mError:\033[0m CMake configuration failed.\n";
        return cfg_res;
    }

    std::cout << "\033[1;36m==>\033[0m Compiling project...\n";
    int bld_res = std::system("cmake --build build -j$(nproc)");
    if (bld_res != 0) {
        std::cerr << "\033[1;31mError:\033[0m Build failed.\n";
        return bld_res;
    }

    std::cout << "\033[1;32m✓ Build finished successfully.\033[0m\n";
    return 0;
}

inline int execute_run() {
    int bld_res = execute_build();
    if (bld_res != 0) {
        return bld_res;
    }

    // Find the primary executable in build/
    fs::path build_dir = fs::current_path() / "build";
    fs::path target_exe;

    for (const auto& entry : fs::directory_iterator(build_dir)) {
        if (entry.is_regular_file()) {
            auto perms = entry.status().permissions();
            if ((perms & fs::perms::owner_exec) != fs::perms::none) {
                std::string fname = entry.path().filename().string();
                if (fname != "cmake" && fname != "ninja" && fname.find('.') == std::string::npos) {
                    target_exe = entry.path();
                    break;
                }
            }
        }
    }

    if (target_exe.empty()) {
        std::cerr << "\033[1;31mError:\033[0m Could not locate compiled executable in build/.\n";
        return 1;
    }

    std::cout << "\033[1;36m==>\033[0m Executing \033[1m" << target_exe.filename().string() << "\033[0m...\n\n";
    return std::system(target_exe.c_str());
}

} // namespace aegon::cli::commands
