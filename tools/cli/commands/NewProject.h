#pragma once

#include "templates/StarterTemplates.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace aegon::cli::commands {

namespace fs = std::filesystem;

inline int execute_new(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "\033[1;31mError:\033[0m Missing project name.\n";
        std::cout << "Usage: aegon new <project_name> [--minimal] [--no-redis] [--no-orm]\n";
        return 1;
    }

    fs::path input_path = args[0];
    fs::path target_dir = input_path.is_absolute() ? input_path : (fs::current_path() / input_path);
    std::string project_name = target_dir.filename().string();
    bool with_orm = true;
    bool with_redis = true;

    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--minimal") {
            with_orm = false;
            with_redis = false;
        } else if (args[i] == "--no-orm") {
            with_orm = false;
        } else if (args[i] == "--no-redis") {
            with_redis = false;
        }
    }

    if (fs::exists(target_dir)) {
        std::cerr << "\033[1;31mError:\033[0m Directory '" << target_dir.string() << "' already exists.\n";
        return 1;
    }

    std::cout << "\033[1;36m==>\033[0m Creating new Aegon C++26 application: \033[1m" << project_name << "\033[0m\n";

    try {
        fs::create_directories(target_dir / "src");

        auto write_file = [](const fs::path& path, const std::string& content) {
            std::ofstream ofs(path);
            if (!ofs) {
                throw std::runtime_error("Failed to write file: " + path.string());
            }
            ofs << content;
        };

        // Write CMakeLists.txt
        write_file(target_dir / "CMakeLists.txt", templates::generate_cmake(project_name, with_orm, with_redis));
        std::cout << "  \033[32m+\033[0m CMakeLists.txt\n";

        // Write src/main.cpp
        write_file(target_dir / "src" / "main.cpp", templates::generate_main(project_name));
        std::cout << "  \033[32m+\033[0m src/main.cpp\n";

        // Write config.yml
        write_file(target_dir / "config.yml", templates::generate_yaml_config(project_name));
        std::cout << "  \033[32m+\033[0m config.yml\n";

        // Write .env and .env.example
        std::string dotenv = templates::generate_dotenv(project_name);
        write_file(target_dir / ".env", dotenv);
        write_file(target_dir / ".env.example", dotenv);
        std::cout << "  \033[32m+\033[0m .env\n";
        std::cout << "  \033[32m+\033[0m .env.example\n";

        // Write .gitignore
        write_file(target_dir / ".gitignore", templates::generate_gitignore());
        std::cout << "  \033[32m+\033[0m .gitignore\n";

        // Write README.md
        write_file(target_dir / "README.md", templates::generate_readme(project_name));
        std::cout << "  \033[32m+\033[0m README.md\n";

        std::cout << "\n\033[1;32m✓ Successfully created " << project_name << "!\033[0m\n\n";
        std::cout << "To get started:\n";
        std::cout << "  \033[33mcd " << project_name << "\033[0m\n";
        std::cout << "  \033[33maegon run\033[0m\n\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\033[1;31mError creating project:\033[0m " << e.what() << "\n";
        return 1;
    }
}

} // namespace aegon::cli::commands
