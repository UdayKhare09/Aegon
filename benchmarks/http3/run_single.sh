#!/usr/bin/env bash
# ==============================================================================
# Aegon Isolated Single-Benchmark Runner for HTTP/3 over QUIC (RFC 9000 / RFC 9114)
# Enforces CPU core partitioning: Server on Cores 0,1, h2load on Cores 2,3,4,5.
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
H2LOAD="${SCRIPT_DIR}/tools/h2load"

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
    swerver)
        PORT=18084
        CMD="${SCRIPT_DIR}/servers/swerver/zig-out/bin/bench_swerver ${THREADS} ${PORT}"
        ;;
    actix|drogon|fiber)
        echo "Framework $FRAMEWORK does not implement RFC 9000 / RFC 9114 HTTP/3 (UNSUPPORTED)."
        exit 0
        ;;
    *)
        echo "Usage: $0 <aegon|swerver> <plaintext|json|user_post> [duration] [concurrency] [streams_per_conn]"
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
echo " Framework:   ${FRAMEWORK} (HTTP/3 over QUIC)"
echo " Endpoint:    ${URL_PATH}"
echo " Duration:    ${DURATION}"
echo " Connections: ${CONCURRENCY} (Streams/Conn: ${STREAMS} -> Total Active: $((CONCURRENCY * STREAMS)))"
echo " Server CPU:  Physical Cores 0, 1       (taskset -c 0,1, 2 threads)"
echo " Client CPU:  Physical Cores 2, 3, 4, 5 (taskset -c 2,3,4,5, 4 threads, h2load --h3)"
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

# Wait for server ready via curl HTTP/3 probe
for i in {1..20}; do
    if curl --http3 -k -s -f "https://127.0.0.1:${PORT}/plaintext" >/dev/null 2>&1; then
        break
    fi
    sleep 0.2
done

if ! curl --http3 -k -s -f "${TARGET_URL}" >/dev/null 2>&1; then
    echo "Error: Server failed to start on port ${PORT} with HTTP/3."
    exit 1
fi

echo "==> Running h2load HTTP/3 measurement (${DURATION} with 3s warmup)..."
taskset -c 2,3,4,5 "${H2LOAD}" --h3 -t4 -c"${CONCURRENCY}" -m"${STREAMS}" -D"${DURATION}" --warm-up-time=3s "${TARGET_URL}"

echo "✓ Benchmark finished cleanly."
