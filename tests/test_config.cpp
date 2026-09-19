#include "config/Config.h"
#include <iostream>
#include <cassert>
#include <fstream>
#include <filesystem>
#include <cstdlib>

using namespace aegon::config;

namespace fs = std::filesystem;

struct ServerConfig {
    std::string host = "localhost";
    int port = 8080;
    bool tls = false;
    double timeout = 30.0;
};

struct DatabaseConfig {
    std::string url = "postgres://localhost:5432/defaultdb";
    int max_connections = 10;
};

struct AppConfig {
    std::string app_name = "default_app";
    ServerConfig server;
    DatabaseConfig database;
    std::vector<std::string> tags;
};

void test_basic_yaml_load_string() {
    std::cout << "[TEST 1] Basic YAML String Loading into C++ Structs..." << std::endl;

    std::string yaml = R"(
app_name: "aegon_core"
server:
  host: "0.0.0.0"
  port: 9000
  tls: true
  timeout: 15.5
database:
  url: "postgres://db.prod:5432/main"
  max_connections: 50
tags:
  - "production"
  - "us-east-1"
)";

    auto res = Config::load_string<AppConfig>(yaml);
    assert(res.has_value());
    auto& cfg = *res;

    assert(cfg.app_name == "aegon_core");
    assert(cfg.server.host == "0.0.0.0");
    assert(cfg.server.port == 9000);
    assert(cfg.server.tls == true);
    assert(cfg.server.timeout == 15.5);
    assert(cfg.database.url == "postgres://db.prod:5432/main");
    assert(cfg.database.max_connections == 50);
    assert(cfg.tags.size() == 2);
    assert(cfg.tags[0] == "production");
    assert(cfg.tags[1] == "us-east-1");

    std::cout << "  -> PASS\n";
}

void test_struct_defaults_preserved() {
    std::cout << "[TEST 2] Partial YAML preserves struct defaults..." << std::endl;

    std::string yaml = R"(
server:
  port: 9443
)";

    auto res = Config::load_string<AppConfig>(yaml);
    assert(res.has_value());
    auto& cfg = *res;

    // Overwritten
    assert(cfg.server.port == 9443);
    // Preserved defaults
    assert(cfg.app_name == "default_app");
    assert(cfg.server.host == "localhost");
    assert(cfg.server.tls == false);
    assert(cfg.server.timeout == 30.0);
    assert(cfg.database.url == "postgres://localhost:5432/defaultdb");
    assert(cfg.database.max_connections == 10);
    assert(cfg.tags.empty());

    std::cout << "  -> PASS\n";
}

void test_env_expansion() {
    std::cout << "[TEST 3] Environment Variable Expansion (${VAR:default})..." << std::endl;

    // 1. Default value fallback
    std::string yaml_default = R"(
server:
  port: ${AEGON_UNSET_PORT_XYZ:7777}
)";
    auto res1 = Config::load_string<AppConfig>(yaml_default);
    assert(res1.has_value());
    assert(res1->server.port == 7777);

    // 2. Set environment variable
    ::setenv("AEGON_APP_ENV_NAME", "super_server", 1);
    std::string yaml_env = R"(
app_name: ${AEGON_APP_ENV_NAME}
server:
  port: ${AEGON_UNSET_PORT_XYZ:8888}
)";
    auto res2 = Config::load_string<AppConfig>(yaml_env);
    assert(res2.has_value());
    assert(res2->app_name == "super_server");
    assert(res2->server.port == 8888);

    // 3. Escaped variable \${VAR}
    std::string raw = R"(val: \${NOT_EXPANDED})";
    auto expanded = EnvExpander::expand(raw);
    assert(expanded.has_value());
    assert(*expanded == "val: ${NOT_EXPANDED}");

    // 4. Missing required variable returns error
    std::string yaml_missing = R"(
app_name: ${TOTALLY_NONEXISTENT_VAR_12345}
)";
    auto res3 = Config::load_string<AppConfig>(yaml_missing);
    assert(!res3.has_value());
    assert(res3.error().message.find("TOTALLY_NONEXISTENT_VAR_12345") != std::string::npos);

    std::cout << "  -> PASS\n";
}

void test_dotenv_parser() {
    std::cout << "[TEST 4] DotEnv Parser functionality..." << std::endl;

    std::string dotenv_content = R"(
# Configuration .env
APP_ENV=staging
PORT=3000
DB_PASS="secret#123"
export SINGLE_QUOTED='hello world'
UNQUOTED_COMMENT=my_value # inline comment
TRAILING_SPACES=test_val   
)";

    auto parsed = DotEnv::parse(dotenv_content);
    assert(parsed["APP_ENV"] == "staging");
    assert(parsed["PORT"] == "3000");
    assert(parsed["DB_PASS"] == "secret#123");
    assert(parsed["SINGLE_QUOTED"] == "hello world");
    assert(parsed["UNQUOTED_COMMENT"] == "my_value");
    assert(parsed["TRAILING_SPACES"] == "test_val");

    std::cout << "  -> PASS\n";
}

void test_multi_file_layering() {
    std::cout << "[TEST 5] Multi-file Layered Overrides..." << std::endl;

    fs::path temp_dir = fs::temp_directory_path() / "aegon_config_test";
    fs::create_directories(temp_dir);

    fs::path base_path = temp_dir / "base.yaml";
    fs::path override_path = temp_dir / "override.yaml";

    {
        std::ofstream base(base_path);
        base << R"(
app_name: "base_application"
server:
  host: "127.0.0.1"
  port: 8080
database:
  url: "postgres://base:5432/db"
)";
    }

    {
        std::ofstream override_file(override_path);
        override_file << R"(
server:
  port: 9090
database:
  max_connections: 100
)";
    }

    auto res = Config::builder<AppConfig>()
        .add_file(base_path.string())
        .add_file(override_path.string())
        .build();

    assert(res.has_value());
    auto& cfg = *res;
    assert(cfg.app_name == "base_application");
    assert(cfg.server.host == "127.0.0.1"); // From base
    assert(cfg.server.port == 9090);         // Overridden by override.yaml
    assert(cfg.database.url == "postgres://base:5432/db"); // From base
    assert(cfg.database.max_connections == 100);            // Overridden

    fs::remove_all(temp_dir);
    std::cout << "  -> PASS\n";
}

void test_dotenv_file_integration() {
    std::cout << "[TEST 6] .env File Integration with YAML..." << std::endl;

    fs::path temp_dir = fs::temp_directory_path() / "aegon_dotenv_test";
    fs::create_directories(temp_dir);
    fs::path env_path = temp_dir / ".env";

    {
        std::ofstream env_f(env_path);
        env_f << "DB_URL=postgres://env_user:secret@10.0.0.5:5432/production\n";
        env_f << "SERVER_PORT=5555\n";
    }

    std::string yaml = R"(
server:
  port: ${SERVER_PORT:8080}
database:
  url: ${DB_URL}
)";

    auto res = Config::builder<AppConfig>()
        .add_dotenv_file(env_path.string())
        .add_string(yaml)
        .build();

    assert(res.has_value());
    assert(res->server.port == 5555);
    assert(res->database.url == "postgres://env_user:secret@10.0.0.5:5432/production");

    fs::remove_all(temp_dir);
    std::cout << "  -> PASS\n";
}

void test_env_prefix_mapping() {
    std::cout << "[TEST 7] Environment Variable Prefix Mapping (AEGON__*)..." << std::endl;

    ::setenv("AEGON__SERVER__PORT", "7788", 1);
    ::setenv("AEGON__SERVER__HOST", "192.168.1.100", 1);
    ::setenv("AEGON__APP_NAME", "prefixed_app", 1);
    ::setenv("AEGON__DATABASE__MAX_CONNECTIONS", "25", 1);

    std::string yaml = R"(
app_name: "initial_name"
server:
  port: 8080
)";

    auto res = Config::builder<AppConfig>()
        .add_string(yaml)
        .add_env_prefix("AEGON__")
        .build();

    assert(res.has_value());
    assert(res->server.port == 7788);
    assert(res->server.host == "192.168.1.100");
    assert(res->app_name == "prefixed_app");
    assert(res->database.max_connections == 25);

    std::cout << "  -> PASS\n";
}

void test_cli_flags_override() {
    std::cout << "[TEST 8] Command-Line Flag Overrides (--server.port=...)..." << std::endl;

    std::vector<std::string> cli_args = {
        "./aegon_app",
        "--server.port=9999",
        "--server.tls=true",
        "--app_name=cli_master",
        "--database.max_connections=75"
    };

    std::string yaml = R"(
app_name: "initial"
server:
  port: 8080
  tls: false
database:
  max_connections: 10
)";

    auto res = Config::builder<AppConfig>()
        .add_string(yaml)
        .add_cli_flags(cli_args)
        .build();

    assert(res.has_value());
    assert(res->server.port == 9999);
    assert(res->server.tls == true);
    assert(res->app_name == "cli_master");
    assert(res->database.max_connections == 75);

    std::cout << "  -> PASS\n";
}

void test_12factor_precedence() {
    std::cout << "[TEST 9] 12-Factor Full Layering Precedence..." << std::endl;

    // Layer 1: Struct Default (8080)
    // Layer 2: Base YAML (8081)
    // Layer 3: Override YAML (8082)
    // Layer 4: DotEnv expansion (${PORT:8080} -> 8083)
    // Layer 5: Env Prefix (AEGON_PREC__SERVER__PORT=8084)
    // Layer 6: CLI Flag (--server.port=8085)

    ::setenv("AEGON_PREC__SERVER__PORT", "8084", 1);
    std::vector<std::string> cli_args = {"./app", "--server.port=8085"};

    // Test up to Layer 2
    auto res2 = Config::builder<AppConfig>()
        .add_string("server:\n  port: 8081\n")
        .build();
    assert(res2.has_value() && res2->server.port == 8081);

    // Test up to Layer 3
    auto res3 = Config::builder<AppConfig>()
        .add_string("server:\n  port: 8081\n")
        .add_string("server:\n  port: 8082\n")
        .build();
    assert(res3.has_value() && res3->server.port == 8082);

    // Test up to Layer 4 (manual add_env or dotenv)
    auto res4 = Config::builder<AppConfig>()
        .add_env("MY_PORT", "8083")
        .add_string("server:\n  port: ${MY_PORT:8081}\n")
        .build();
    assert(res4.has_value() && res4->server.port == 8083);

    // Test up to Layer 5 (Prefix override beats YAML)
    auto res5 = Config::builder<AppConfig>()
        .add_string("server:\n  port: 8082\n")
        .add_env_prefix("AEGON_PREC__")
        .build();
    assert(res5.has_value() && res5->server.port == 8084);

    // Test up to Layer 6 (CLI flag beats Prefix override)
    auto res6 = Config::builder<AppConfig>()
        .add_string("server:\n  port: 8082\n")
        .add_env_prefix("AEGON_PREC__")
        .add_cli_flags(cli_args)
        .build();
    assert(res6.has_value() && res6->server.port == 8085);

    std::cout << "  -> PASS\n";
}

void test_error_handling() {
    std::cout << "[TEST 10] Rich Diagnostic Error Handling..." << std::endl;

    // 1. Missing mandatory file
    auto res1 = Config::builder<AppConfig>()
        .add_file("/non/existent/path/config.yaml", /*optional=*/false)
        .build();
    assert(!res1.has_value());
    assert(res1.error().message.find("not found") != std::string::npos);

    // 2. Missing optional file does not error
    auto res2 = Config::builder<AppConfig>()
        .add_file("/non/existent/path/optional.yaml", /*optional=*/true)
        .build();
    assert(res2.has_value());
    assert(res2->server.port == 8080); // Default preserved

    // 3. Syntax error in YAML
    std::string malformed_yaml = R"(
server:
  port: [not a number
)";
    auto res3 = Config::load_string<AppConfig>(malformed_yaml);
    assert(!res3.has_value());
    assert(!res3.error().message.empty());

    std::cout << "  -> PASS\n";
}

int main() {
    std::cout << "\n============================================\n";
    std::cout << "   RUNNING AEGON CONFIG TEST SUITE (YAML)   \n";
    std::cout << "============================================\n\n";

    test_basic_yaml_load_string();
    test_struct_defaults_preserved();
    test_env_expansion();
    test_dotenv_parser();
    test_multi_file_layering();
    test_dotenv_file_integration();
    test_env_prefix_mapping();
    test_cli_flags_override();
    test_12factor_precedence();
    test_error_handling();

    std::cout << "\n============================================\n";
    std::cout << "   ALL CONFIG SUITE TESTS PASSED (100%)    \n";
    std::cout << "============================================\n\n";
    return 0;
}
