# Configuration Engine (YAML & 12-Factor)

Aegon provides a dedicated, cloud-native configuration processor library: `aegon_config`. Built in modern C++26 and powered by compile-time reflection via Glaze's YAML 1.2 engine, it delivers type-safe, zero-boilerplate configuration deserialization conforming strictly to the **12-Factor App** methodology.

> [!NOTE]
> Aegon's configuration engine is designed exclusively for **YAML 1.2** and environment-driven architectures. It avoids JSON configuration files in favor of human-readable, comment-friendly YAML with nested schemas.

---

## Key Features

- **Type-Safe Aggregate Reflection**: Automatically maps nested YAML mappings and sequences to native C++ aggregate structs without manual serialization boilerplate.
- **Strict 12-Factor Layered Precedence**:
  1. Default C++ Struct Values
  2. Base YAML (`config.yaml`)
  3. Environment YAML (`config.production.yaml`)
  4. DotEnv (`.env` file)
  5. Dynamic Environment Expansion (`${DATABASE_URL}`, `${PORT:8080}`)
  6. Prefix Environment Overrides (`AEGON__SERVER__PORT=9000`)
  7. CLI Argument Overrides (`--server.port=9090`)
- **First-Class `.env` Parser**: Parses standard `.env` files with support for quotes (`"..."`, `'...'`), inline comments (`#`), and `export` prefixes.
- **Dynamic Variable Expansion**: Expands `${VAR}` (mandatory) and `${VAR:default}` syntax within YAML content, with support for literal escaping (`\${VAR}`).
- **Rich Diagnostic Error Reporting**: Provides exact file, line, and column error messages for malformed YAML or missing mandatory variables via `std::expected<T, ConfigError>`.

---

## Defining Configuration Schemas

Configuration schemas in Aegon are clean, idiomatic C++ structs with default member initializers:

```cpp
#include <string>
#include <vector>

struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    bool tls = false;
    double timeout = 30.0;
};

struct DatabaseConfig {
    std::string url = "postgres://localhost:5432/app";
    int max_connections = 20;
};

struct AppConfig {
    std::string app_name = "aegon_service";
    ServerConfig server;
    DatabaseConfig database;
    std::vector<std::string> tags;
};
```

---

## Quick Start: One-Liner Loading

For simple services that read from a single YAML file on disk:

```cpp
#include "config/Config.h"
#include <iostream>

using namespace aegon::config;

int main() {
    auto result = Config::load<AppConfig>("config.yaml");
    if (!result) {
        std::cerr << "Configuration Error: " << result.error().format() << "\n";
        return 1;
    }

    AppConfig config = std::move(*result);
    std::cout << "Starting " << config.app_name 
              << " on port " << config.server.port << "\n";
    return 0;
}
```

---

## Fluent Layered Builder (`ConfigBuilder`)

For production cloud-native applications, use `Config::builder<T>()` to chain multiple layered sources:

```cpp
#include "config/Config.h"

using namespace aegon::config;

int main(int argc, char* argv[]) {
    auto config = Config::builder<AppConfig>()
        // 1. Load base configuration file
        .add_file("config/base.yaml")
        // 2. Conditionally overlay environment-specific file (optional if not found)
        .add_file("config/production.yaml", /*optional=*/true)
        // 3. Load local secrets from .env (optional in containerized clouds)
        .add_dotenv_file(".env", /*optional=*/true)
        // 4. Map environment variables with prefix AEGON__ (e.g. AEGON__SERVER__PORT)
        .add_env_prefix("AEGON__")
        // 5. Apply command-line flag overrides (e.g. --server.port=9090)
        .add_cli_flags(argc, argv)
        .build();

    if (!config) {
        throw std::runtime_error(config.error().format());
    }

    // Fully populated & validated config instance
    return run_server(*config);
}
```

---

## Environment Variable Expansion

YAML configuration files can embed dynamic runtime environment variables:

```yaml
app_name: "payment-gateway"

server:
  host: ${HOST:0.0.0.0}
  port: ${PORT:8080}
  tls: ${ENABLE_TLS:false}

database:
  url: ${DATABASE_URL}                # Mandatory: throws ConfigError if not set
  max_connections: ${DB_POOL_SIZE:25}
```

### Syntax Rules
| Syntax | Meaning |
| :--- | :--- |
| `${VAR}` | Mandatory variable. If unset in the environment, builder returns `ConfigError`. |
| `${VAR:default}` | Optional variable with fallback. If `VAR` is unset, uses `default`. |
| `\${LITERAL}` | Escaped token. Resolves to the literal text `${LITERAL}` without evaluation. |

---

## Environment Variable Prefix Mapping

You can configure cloud orchestrators (such as Kubernetes or Docker Swarm) to override deeply nested keys via environment variables using double underscores (`__`) as delimiters:

```bash
export AEGON__SERVER__PORT=9000
export AEGON__SERVER__HOST="127.0.0.1"
export AEGON__DATABASE__MAX_CONNECTIONS=50
```

Registering `.add_env_prefix("AEGON__")` automatically maps these to their corresponding YAML struct fields:

```cpp
auto config = Config::builder<AppConfig>()
    .add_file("config.yaml")
    .add_env_prefix("AEGON__")
    .build();
```

---

## CLI Flag Overrides

Command-line flags pass runtime overrides directly to the builder using dot notation (`.`) or double underscores (`__`):

```bash
./my_service --server.port=9999 --server.tls=true --database.max_connections=100
```

In your application entrypoint:

```cpp
auto config = Config::builder<AppConfig>()
    .add_file("config.yaml")
    .add_cli_flags(argc, argv)
    .build();
```

---

## Standalone `.env` Parsing

`DotEnv` can also be used as a standalone parser to populate process environment tables or inspect key-value configurations:

```cpp
#include "config/DotEnv.h"

// Parse from a string
auto env = aegon::config::DotEnv::parse(R"(
    APP_ENV=production
    SECRET_KEY="super-secret#value"
    export PORT=8080
)");

// Or load file and automatically export to system environment (setenv)
auto loaded = aegon::config::DotEnv::load_file(".env", /*set_environment=*/true);
```

---

## Error Handling & Diagnostics

When deserialization or file loading fails, `Config::build()` returns a descriptive `ConfigError`:

```cpp
auto result = Config::builder<AppConfig>()
    .add_file("missing_file.yaml")
    .build();

if (!result) {
    const ConfigError& err = result.error();
    std::cerr << "File: " << err.file_path << "\n"
              << "Line: " << err.line << ", Column: " << err.column << "\n"
              << "Details: " << err.message << "\n";
}
```
