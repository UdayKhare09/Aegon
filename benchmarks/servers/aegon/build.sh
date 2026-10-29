#!/usr/bin/env bash
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

echo "==> Configuring and building Aegon Unified Benchmark Server..."
cmake -B "${ROOT_DIR}/build" -DAEGON_BUILD_BENCHMARKS=ON
ninja -C "${ROOT_DIR}/build" bench_aegon
echo "✓ Aegon Unified Server built: ${ROOT_DIR}/build/benchmarks/bench_aegon"
