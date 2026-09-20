# Aegon CLI Tooling

Aegon provides a first-class developer command-line interface (`aegon`) to streamline project generation, environment diagnostics, builds, and rapid development execution.

---

## Installation

When installing Aegon on your system (via building and running `sudo cmake --install build`), the `aegon` binary is installed directly into your system PATH (`/usr/bin/aegon` or `/usr/local/bin/aegon`).

To verify your installation:

```bash
aegon version
```

Output:
```
       _                             
      / \   ___  __ _  ___  _ __     
     / _ \ / _ \/ _` |/ _ \| '_ \    
    / ___ \  __/ (_| | (_) | | | |   
   /_/   \_\___|\__, |\___/|_| |_|   
                |___/                
  Next-Gen Linux-Native C++26 Web Framework

Version:       0.1.a2
C++ Standard:  C++26
io_uring:      Active
SIMD Engine:   AVX-512 / AVX2 / SSE4.2
```

---

## Commands Overview

| Command | Description |
| :--- | :--- |
| `aegon new <project>` | Scaffolds a new, production-ready C++26 Aegon project. |
| `aegon doctor` | Runs hardware, kernel, compiler, and `io_uring` system diagnostics. |
| `aegon build` | Automatically invokes CMake and Ninja to build the current project. |
| `aegon run` | Builds and immediately runs the project application. |
| `aegon version` | Displays framework version and hardware acceleration status. |
| `aegon help` | Displays help message and CLI usage options. |

---

## `aegon doctor` (System Diagnostics)

Because Aegon operates directly against kernel `io_uring` rings and modern CPU vector units, `aegon doctor` analyzes your environment to ensure optimal runtime capabilities:

```bash
aegon doctor
```

Example diagnostic report:
```
==> Checking Aegon system environment & prerequisites...

Kernel:
  ✓ Linux kernel: 6.13.0-arch1-1
  ✓ io_uring subsystem: Available & Initialized (ring queue size 8 ok)
  ✓ io_uring fast poll: Supported
  ✓ io_uring nodrop: Supported

Hardware & Vector Acceleration:
  ✓ AVX-512: Supported
  ✓ AVX2: Supported
  ✓ RDRAND: Supported (Hardware Entropy)
  ✓ AES-NI: Supported (Hardware Cryptography)

Toolchain:
  ✓ C++ Compiler: GNU 16.2.1
  ✓ C++26 Support: Available
  ✓ CMake: 3.31.5
  ✓ Ninja: 1.13.1

Headers:
  ✓ Aegon headers: Found in /usr/include or /usr/local/include

✓ System is ready for high-performance Aegon development!
```

---

## `aegon new` (Project Generator)

Create a new application skeleton in seconds:

```bash
aegon new my_service
```

### Options & Flags

| Option | Description |
| :--- | :--- |
| `--minimal` | Scaffolds a lightweight HTTP-only application (disables ORM and Redis). |
| `--no-orm` | Excludes SQL ORM targets (`Aegon::orm`, PostgreSQL, SQLite3). |
| `--no-redis` | Excludes Redis target (`Aegon::redis`). |

### Generated Project Structure

```
my_service/
├── CMakeLists.txt     # Modern CMake configuration with find_package(Aegon REQUIRED)
├── config.yml         # Structured application configuration with ${VAR:default}
├── .env               # Environment-specific overrides (git-ignored)
├── .env.example       # Template for production and CI/CD variables
├── .gitignore         # Build and secret ignore rules
├── README.md          # Project instructions and getting started guide
└── src/
    └── main.cpp       # Typed AppConfig, async coroutine + sync routes
```

### Layered Configuration in Action

Generated projects use Aegon's 3-layer configuration hierarchy:

1. **`config.yml` (Base configuration)**:
   ```yaml
   app:
     name: ${APP_NAME:my_service}
     env: ${APP_ENV:development}

   server:
     host: ${HOST:0.0.0.0}
     port: ${PORT:8080}
     threads: ${THREADS:0}
   ```

2. **`.env` (Environment overrides)**:
   ```env
   APP_ENV=development
   PORT=8080
   DATABASE_URL=sqlite:///app.db
   ```

3. **CLI Flags (Runtime overrides)**:
   ```bash
   ./build/my_service --server.port=9090
   ```

In `src/main.cpp`:
```cpp
auto config_res = aegon::config::Config::builder<AppConfig>()
    .add_file("config.yml", /*optional=*/true)
    .add_dotenv_file(".env", /*optional=*/true)
    .add_cli_flags(argc, argv)
    .build();
```

---

## `aegon build` & `aegon run`

Build and execute applications without memorizing CMake arguments:

```bash
cd my_service

# Build debug binary (default)
aegon build

# Build optimized release binary
aegon build --release

# Build and start server
aegon run

# Pass arguments directly to the application
aegon run --release -- --server.port=9000
```
