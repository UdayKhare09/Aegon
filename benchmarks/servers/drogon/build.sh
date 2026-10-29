#!/usr/bin/env bash
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> Building Drogon Benchmark Server..."
cmake -B "${SCRIPT_DIR}/build" -S "${SCRIPT_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C "${SCRIPT_DIR}/build"

cat << 'EOF' > "${SCRIPT_DIR}/bench_drogon"
#!/usr/bin/env bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${SCRIPT_DIR}/build/bench_drogon_bin"

# Jemalloc optimization from HttpArena:
# jsoncpp allocates a node per JSON value; jemalloc drastically reduces lock contention across worker threads.
if [ -f "/usr/lib/libjemalloc.so.2" ]; then
    export LD_PRELOAD="/usr/lib/libjemalloc.so.2:${LD_PRELOAD}"
elif [ -f "/usr/lib/libjemalloc.so" ]; then
    export LD_PRELOAD="/usr/lib/libjemalloc.so:${LD_PRELOAD}"
fi

export PATH="$HOME/.local/bin:$PATH"
export LD_LIBRARY_PATH="$HOME/.local/lib:${LD_LIBRARY_PATH}"

exec "${BIN}" "$@"
EOF

chmod +x "${SCRIPT_DIR}/bench_drogon"
echo "✓ Drogon Server built: ${SCRIPT_DIR}/bench_drogon"

