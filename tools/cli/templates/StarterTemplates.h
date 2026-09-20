#pragma once

#include <string>
#include <string_view>

namespace aegon::cli::templates {

inline std::string generate_cmake(std::string_view project_name, bool with_orm = true, bool with_redis = true) {
    std::string s;
    s += "cmake_minimum_required(VERSION 3.25)\n";
    s += "project(" + std::string(project_name) + " LANGUAGES CXX)\n\n";
    s += "set(CMAKE_CXX_STANDARD 26)\n";
    s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    s += "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";
    s += "# Optimize for modern architecture\n";
    s += "if(CMAKE_CXX_COMPILER_ID MATCHES \"GNU|Clang\")\n";
    s += "    add_compile_options(-O3 -march=native -Wall -Wextra)\n";
    s += "endif()\n\n";
    s += "# Find Aegon Asynchronous Framework\n";
    s += "find_package(Aegon REQUIRED)\n\n";
    s += "add_executable(" + std::string(project_name) + "\n";
    s += "    src/main.cpp\n";
    s += ")\n\n";
    s += "target_link_libraries(" + std::string(project_name) + " PRIVATE\n";
    s += "    Aegon::http\n";
    s += "    Aegon::core\n";
    s += "    Aegon::config\n";
    if (with_orm) {
        s += "    Aegon::orm\n";
    }
    if (with_redis) {
        s += "    Aegon::redis\n";
    }
    s += ")\n\n";
    s += "# Symlink config files to build directory for convenience\n";
    s += "file(CREATE_LINK ${CMAKE_CURRENT_SOURCE_DIR}/config.yml ${CMAKE_CURRENT_BINARY_DIR}/config.yml SYMBOLIC COPY_ON_ERROR)\n";
    s += "file(CREATE_LINK ${CMAKE_CURRENT_SOURCE_DIR}/.env ${CMAKE_CURRENT_BINARY_DIR}/.env SYMBOLIC COPY_ON_ERROR)\n";
    return s;
}

inline std::string generate_yaml_config(std::string_view project_name) {
    std::string s;
    s += "# ==============================================================================\n";
    s += "# Aegon Application Configuration\n";
    s += "# Supports environment variable expansion (${VAR:default}) and CLI flags\n";
    s += "# ==============================================================================\n\n";
    s += "app:\n";
    s += "  name: ${APP_NAME:" + std::string(project_name) + "}\n";
    s += "  env: ${APP_ENV:development}\n";
    s += "  log_level: ${LOG_LEVEL:info}\n\n";
    s += "server:\n";
    s += "  host: ${HOST:0.0.0.0}\n";
    s += "  port: ${PORT:8080}\n";
    s += "  threads: ${THREADS:0}\n\n";
    s += "database:\n";
    s += "  url: \"${DATABASE_URL:sqlite:///app.db}\"\n";
    s += "  pool_size: ${DB_POOL_SIZE:10}\n\n";
    s += "redis:\n";
    s += "  url: \"${REDIS_URL:redis://127.0.0.1:6379}\"\n";
    return s;
}

inline std::string generate_dotenv(std::string_view project_name) {
    std::string s;
    s += "# ==============================================================================\n";
    s += "# Environment Overrides for " + std::string(project_name) + "\n";
    s += "# ==============================================================================\n";
    s += "APP_NAME=" + std::string(project_name) + "\n";
    s += "APP_ENV=development\n";
    s += "LOG_LEVEL=info\n\n";
    s += "# Server settings\n";
    s += "HOST=0.0.0.0\n";
    s += "PORT=8080\n";
    s += "THREADS=0\n\n";
    s += "# Database & Cache settings\n";
    s += "DATABASE_URL=sqlite:///app.db\n";
    s += "DB_POOL_SIZE=10\n";
    s += "REDIS_URL=redis://127.0.0.1:6379\n";
    return s;
}

inline std::string generate_main(std::string_view project_name) {
    std::string s;
    s += "#include <aegon/http/Server.h>\n";
    s += "#include <aegon/core/Task.h>\n";
    s += "#include <aegon/config/Config.h>\n";
    s += "#include <aegon/data/types/UUID.h>\n";
    s += "#include <aegon/Version.h>\n";
    s += "#include <iostream>\n";
    s += "#include <glaze/glaze.hpp>\n\n";
    s += "struct ServerConfig {\n";
    s += "    std::string host = \"0.0.0.0\";\n";
    s += "    uint16_t port = 8080;\n";
    s += "    size_t threads = 0;\n";
    s += "};\n\n";
    s += "struct DatabaseConfig {\n";
    s += "    std::string url = \"sqlite:///app.db\";\n";
    s += "    size_t pool_size = 10;\n";
    s += "};\n\n";
    s += "struct RedisConfig {\n";
    s += "    std::string url = \"redis://127.0.0.1:6379\";\n";
    s += "};\n\n";
    s += "struct AppMetaConfig {\n";
    s += "    std::string name = \"" + std::string(project_name) + "\";\n";
    s += "    std::string env = \"development\";\n";
    s += "    std::string log_level = \"info\";\n";
    s += "};\n\n";
    s += "struct AppConfig {\n";
    s += "    AppMetaConfig app;\n";
    s += "    ServerConfig server;\n";
    s += "    DatabaseConfig database;\n";
    s += "    RedisConfig redis;\n";
    s += "};\n\n";
    s += "struct WelcomeDTO {\n";
    s += "    std::string app;\n";
    s += "    std::string version;\n";
    s += "    std::string env;\n";
    s += "    std::string message;\n";
    s += "    std::string request_id;\n";
    s += "};\n\n";
    s += "int main(int argc, char* argv[]) {\n";
    s += "    // 1. Layered configuration: config.yml (base defaults) -> .env (env overrides) -> CLI flags\n";
    s += "    auto config_res = aegon::config::Config::builder<AppConfig>()\n";
    s += "        .add_file(\"config.yml\", /*optional=*/true)\n";
    s += "        .add_dotenv_file(\".env\", /*optional=*/true)\n";
    s += "        .add_cli_flags(argc, argv)\n";
    s += "        .build();\n\n";
    s += "    if (!config_res) {\n";
    s += "        std::cerr << \"Configuration error: \" << config_res.error().message << std::endl;\n";
    s += "        return 1;\n";
    s += "    }\n\n";
    s += "    const auto& config = *config_res;\n\n";
    s += "    std::cout << \"Starting \" << config.app.name << \" (Aegon v\" << aegon::version.string\n";
    s += "              << \") [\" << config.app.env << \" mode]...\" << std::endl;\n\n";
    s += "    // 2. Initialize high-performance asynchronous server\n";
    s += "    aegon::http::Server server;\n\n";
    s += "    // 3. Define routes (both async coroutines and zero-overhead sync handlers)\n";
    s += "    // Asynchronous coroutine route (ready for co_await db/redis/network I/O)\n";
    s += "    server.router().get(\"/\", [&config](aegon::http::Context& ctx) -> aegon::core::Task<void> {\n";
    s += "        WelcomeDTO dto{\n";
    s += "            .app = config.app.name,\n";
    s += "            .version = std::string(aegon::version.string),\n";
    s += "            .env = config.app.env,\n";
    s += "            .message = \"Welcome to Aegon modern C++26 high-performance service!\",\n";
    s += "            .request_id = aegon::data::UUIDGenerator::v7().to_string()\n";
    s += "        };\n";
    s += "        ctx.res().json(dto);\n";
    s += "        co_return;\n";
    s += "    });\n\n";
    s += "    // Synchronous route (zero-overhead, no coroutine frame allocation)\n";
    s += "    server.router().get(\"/health\", [](aegon::http::Context& ctx) {\n";
    s += "        ctx.res().text(\"OK\");\n";
    s += "    });\n\n";
    s += "    // 4. Run HTTP/1.1, HTTP/2, and HTTP/3 event loop\n";
    s += "    std::cout << \"Listening on http://\" << config.server.host << \":\" << config.server.port << std::endl;\n";
    s += "    server.listen(config.server.port, config.server.host);\n";
    s += "    if (config.server.threads > 0) {\n";
    s += "        server.run(config.server.threads);\n";
    s += "    } else {\n";
    s += "        server.run();\n";
    s += "    }\n";
    s += "    return 0;\n";
    s += "}\n";
    return s;
}

inline std::string generate_gitignore() {
    return "build/\n"
           "cmake-build-*/\n"
           ".cache/\n"
           ".env\n"
           "*.o\n"
           "*.a\n"
           "*.so\n"
           "compile_commands.json\n";
}

inline std::string generate_readme(std::string_view project_name) {
    std::string s;
    s += "# " + std::string(project_name) + "\n\n";
    s += "A high-performance modern C++26 asynchronous web service built with [Aegon](https://github.com/UdayKhare09/Aegon).\n\n";
    s += "## Features\n";
    s += "- **Asynchronous I/O**: `io_uring` kernel event loop with HTTP/1.1, HTTP/2, and HTTP/3 QUIC.\n";
    s += "- **Layered Configuration**: Seamless `.env` variables, `config.yml` with `${VAR:default}` expansion, and CLI overrides.\n";
    s += "- **Compile-time Reflection**: Zero-overhead JSON/YAML serialization via Glaze.\n";
    s += "- **Zero-Allocation Types**: SIMD-accelerated UUID v4/v7 and fast memory-mapped structures.\n\n";
    s += "## Configuration\n\n";
    s += "Settings are loaded in layered priority:\n";
    s += "1. `config.yml` (base application configuration)\n";
    s += "2. `.env` file (local secrets and environment overrides)\n";
    s += "3. CLI flags (e.g., `--server.port=9090` runtime overrides)\n\n";
    s += "## Requirements\n";
    s += "- Linux (kernel 5.10+ recommended for `io_uring`)\n";
    s += "- GCC 14+ or Clang 18+ (C++26 support)\n";
    s += "- CMake 3.25+ and Ninja\n";
    s += "- Aegon Framework (`pacman -S aegon` or compiled from source)\n\n";
    s += "## Build & Run\n\n";
    s += "```bash\n";
    s += "# Run via aegon CLI:\n";
    s += "aegon run\n\n";
    s += "# Or manually via CMake:\n";
    s += "cmake -B build -G Ninja\n";
    s += "cmake --build build\n";
    s += "./build/" + std::string(project_name) + "\n";
    s += "```\n";
    return s;
}

} // namespace aegon::cli::templates
