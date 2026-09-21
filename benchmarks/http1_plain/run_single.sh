#!/usr/bin/env bash
# ==============================================================================
# Aegon Isolated Single-Benchmark Runner (One at a Time)
# Enforces CPU core partitioning: Server on Cores 0-3, wrk on Cores 4-5.
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

FRAMEWORK="${1:-aegon}"
ENDPOINT="${2:-plaintext}"
DURATION="${3:-10s}"
CONCURRENCY="${4:-100}"
THREADS=2 # 2 worker threads pinned to 2 physical cores (0, 1)

case $FRAMEWORK in
    aegon)
        PORT=18080
        CMD="${SCRIPT_DIR}/build/aegon/bench_aegon ${THREADS} ${PORT}"
        ;;
    actix)
        PORT=18081
        CMD="${SCRIPT_DIR}/servers/actix/target/release/bench_actix ${THREADS} ${PORT}"
        ;;
    fiber)
        PORT=18082
        CMD="${SCRIPT_DIR}/servers/fiber/bench_fiber ${THREADS} ${PORT}"
        ;;
    drogon)
        PORT=18083
        CMD="${SCRIPT_DIR}/build/drogon/bench_drogon ${THREADS} ${PORT}"
        ;;
    swerver)
        PORT=18084
        CMD="${SCRIPT_DIR}/servers/swerver/zig-out/bin/bench_swerver ${THREADS} ${PORT}"
        ;;
    *)
        echo "Usage: $0 <aegon|actix|fiber|drogon|swerver> <plaintext|json|user_post> [duration] [concurrency]"
        exit 1
        ;;
esac

case $ENDPOINT in
    plaintext)
        URL_PATH="/plaintext"
        ;;
    json)
        URL_PATH="/json"
        ;;
    user_post|users)
        URL_PATH="/users/42/posts/101"
        ;;
    *)
        echo "Unknown endpoint: $ENDPOINT. Available: plaintext, json, user_post"
        exit 1
        ;;
esac

TARGET_URL="http://127.0.0.1:${PORT}${URL_PATH}"

echo "=============================================================================="
echo " Framework:   ${FRAMEWORK}"
echo " Endpoint:    ${URL_PATH}"
echo " Duration:    ${DURATION}"
echo " Concurrency: ${CONCURRENCY} connections"
echo " Server CPU:  Physical Cores 0, 1       (taskset -c 0,1, 2 threads)"
echo " Client CPU:  Physical Cores 2, 3, 4, 5 (taskset -c 2,3,4,5, 4 threads)"
echo "=============================================================================="

# Launch server pinned to cores 0, 1
taskset -c 0,1 ${CMD} >/dev/null 2>&1 &
SERVER_PID=$!

cleanup() {
    kill $SERVER_PID 2>/dev/null || true
    pkill -P $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    # For Go prefork child processes
    if [ "$FRAMEWORK" = "fiber" ]; then
        pkill -f bench_fiber 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

# Wait for server ready
for i in {1..20}; do
    if curl -s -f "http://127.0.0.1:${PORT}/plaintext" >/dev/null 2>&1; then
        break
    fi
    sleep 0.2
done

if ! curl -s -f "${TARGET_URL}" >/dev/null 2>&1; then
    echo "Error: Server failed to start on port ${PORT}."
    exit 1
fi

echo "==> [1/2] Running 3s warmup..."
taskset -c 2,3,4,5 wrk -t4 -c"${CONCURRENCY}" -d3s "${TARGET_URL}" >/dev/null 2>&1 || true
sleep 1

echo "==> [2/2] Running measurement (${DURATION})..."
taskset -c 2,3,4,5 wrk -t4 -c"${CONCURRENCY}" -d"${DURATION}" --latency "${TARGET_URL}"

echo "✓ Benchmark finished cleanly."
