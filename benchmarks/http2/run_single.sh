#!/usr/bin/env bash
# ==============================================================================
# Aegon Isolated Single-Benchmark Runner for HTTP/2
# Enforces CPU core partitioning: Server on Cores 0,1, h2load on Cores 2,3,4,5.
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

FRAMEWORK="${1:-aegon}"
ENDPOINT="${2:-plaintext}"
DURATION="${3:-10s}"
CONCURRENCY="${4:-100}"
STREAMS="${5:-10}"
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
    swerver)
        PORT=18084
        CMD="${SCRIPT_DIR}/servers/swerver/zig-out/bin/bench_swerver ${THREADS} ${PORT}"
        ;;
    drogon|fiber)
        echo "Framework $FRAMEWORK does not implement RFC 7540 HTTP/2 (UNSUPPORTED)."
        exit 0
        ;;
    *)
        echo "Usage: $0 <aegon|actix|swerver> <plaintext|json|user_post> [duration] [concurrency] [streams_per_conn]"
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

TARGET_URL="https://127.0.0.1:${PORT}${URL_PATH}"

echo "=============================================================================="
echo " Framework:   ${FRAMEWORK} (HTTP/2 Multiplexing)"
echo " Endpoint:    ${URL_PATH}"
echo " Duration:    ${DURATION}"
echo " Connections: ${CONCURRENCY} (Streams/Conn: ${STREAMS} -> Total Active: $((CONCURRENCY * STREAMS)))"
echo " Server CPU:  Physical Cores 0, 1       (taskset -c 0,1, 2 threads)"
echo " Client CPU:  Physical Cores 2, 3, 4, 5 (taskset -c 2,3,4,5, 4 threads, h2load)"
echo "=============================================================================="

# Launch server pinned to cores 0, 1
taskset -c 0,1 ${CMD} >/dev/null 2>&1 &
SERVER_PID=$!

cleanup() {
    kill $SERVER_PID 2>/dev/null || true
    pkill -P $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Wait for server ready
for i in {1..20}; do
    if curl -k -s -f "https://127.0.0.1:${PORT}/plaintext" >/dev/null 2>&1; then
        break
    fi
    sleep 0.2
done

if ! curl -k -s -f "${TARGET_URL}" >/dev/null 2>&1; then
    echo "Error: Server failed to start on port ${PORT}."
    exit 1
fi

echo "==> Running h2load measurement (${DURATION} with 3s warmup)..."
taskset -c 2,3,4,5 h2load -t4 -c"${CONCURRENCY}" -m"${STREAMS}" -D"${DURATION}" --warm-up-time=3s "${TARGET_URL}"

echo "✓ Benchmark finished cleanly."
