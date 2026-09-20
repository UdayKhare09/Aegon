#include "commands/NewProject.h"
#include "commands/Doctor.h"
#include "commands/VersionCmd.h"
#include "commands/BuildCmd.h"
#include <iostream>
#include <string>
#include <vector>

void print_help() {
    std::cout << "\033[1;36m";
    std::cout << R"banner(
    ___                               
   /   |  ___  ____ _____  ____       
  / /| | / _ \/ __ `/ __ \/ __ \
 / ___ |/  __/ /_/ / /_/ / / / /      
/_/  |_|\___/\__, /\____/_/ /_/       
            /____/                    
)banner" << "\033[0m\n";

    std::cout << "\033[1mAegon CLI\033[0m - High-Performance C++26 Web & Data Development Tool\n\n";
    std::cout << "\033[1mUsage:\033[0m aegon <command> [arguments] [options]\n\n";
    std::cout << "\033[1mAvailable Commands:\033[0m\n";
    std::cout << "  \033[1;32mnew <name>\033[0m      Scaffold a modern C++26 Aegon service\n";
    std::cout << "                    Options: \033[90m--minimal, --no-orm, --no-redis\033[0m\n";
    std::cout << "  \033[1;32mdoctor\033[0m          Check system, kernel, CPU vector pipelines & dependencies\n";
    std::cout << "  \033[1;32mbuild\033[0m           Configure and compile current Aegon project\n";
    std::cout << "  \033[1;32mrun\033[0m             Compile and run current Aegon project\n";
    std::cout << "  \033[1;32mversion\033[0m         Display Aegon version, compiler, and SIMD status\n";
    std::cout << "  \033[1;32mhelp\033[0m            Show this help information\n\n";
    std::cout << "\033[1mExamples:\033[0m\n";
    std::cout << "  aegon new my_api\n";
    std::cout << "  aegon doctor\n";
    std::cout << "  cd my_api && aegon run\n\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }

    std::string command = argv[1];
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    if (command == "new") {
        return aegon::cli::commands::execute_new(args);
    } else if (command == "doctor") {
        return aegon::cli::commands::execute_doctor();
    } else if (command == "version" || command == "--version" || command == "-v") {
        return aegon::cli::commands::execute_version();
    } else if (command == "build") {
        return aegon::cli::commands::execute_build();
    } else if (command == "run") {
        return aegon::cli::commands::execute_run();
    } else if (command == "help" || command == "--help" || command == "-h") {
        print_help();
        return 0;
    } else {
        std::cerr << "\033[1;31mUnknown command:\033[0m '" << command << "'\n\n";
        print_help();
        return 1;
    }
}
