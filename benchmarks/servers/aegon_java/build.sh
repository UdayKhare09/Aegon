#!/usr/bin/env bash
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

echo "==> Building Aegon Java Benchmark Server..."
mvn -f "${SCRIPT_DIR}/pom.xml" clean package -DskipTests
chmod +x "${SCRIPT_DIR}/bench_aegon_java"
echo "✓ Aegon Java Server built: ${SCRIPT_DIR}/bench_aegon_java"

