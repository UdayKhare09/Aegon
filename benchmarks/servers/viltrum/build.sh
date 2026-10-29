#!/usr/bin/env bash
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PATH="$HOME/.local/bin:$PATH"

echo "==> Building Viltrum Unified Benchmark Server (-prod -O3 -march=native)..."
v -prod -cc gcc -cflags "-O3 -march=native" -o "${SCRIPT_DIR}/bench_viltrum" "${SCRIPT_DIR}/main.v"
echo "✓ Viltrum Unified Server built: ${SCRIPT_DIR}/bench_viltrum"
