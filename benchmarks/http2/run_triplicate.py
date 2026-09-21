#!/usr/bin/env python3
import sys
import os
import time
import subprocess
import re

# Topology: 2 Cores for Server (0,1), 4 Cores for h2load Client (2,3,4,5)
FRAMEWORKS = {
    "aegon": {
        "port": 18080,
        "cmd": ["benchmarks/http2/build/aegon/bench_aegon", "2", "18080"],
    },
    "actix": {
        "port": 18081,
        "cmd": ["benchmarks/http2/servers/actix/target/release/bench_actix", "2", "18081"],
    },
    "swerver": {
        "port": 18084,
        "cmd": ["benchmarks/http2/servers/swerver/zig-out/bin/bench_swerver", "2", "18084"],
    },
}

ENDPOINTS = {
    "plaintext": "/plaintext",
    "json": "/json",
    "user_post": "/users/42/posts/101",
    "users": "/users/42/posts/101",
}

def parse_time_us(val_str):
    m = re.match(r"([0-9.]+)(us|ms|s)", val_str.strip())
    if not m:
        return 0.0
    val = float(m.group(1))
    unit = m.group(2)
    if unit == "ms":
        return val * 1000.0
    elif unit == "s":
        return val * 1000000.0
    return val

def parse_h2load_output(output):
    # finished in 5.00s, 1479890.00 req/s, 53.63MB/s
    rps_m = re.search(r"finished in .*?,\s+([0-9.]+)\s+req/s", output)
    rps = float(rps_m.group(1)) if rps_m else 0.0

    # requests: 2959780 total, 2959780 started, 2959780 done, 2959780 succeeded, 0 failed, 0 errored, 0 timeout
    req_m = re.search(r"requests:\s+(\d+)\s+total.*?(\d+)\s+succeeded.*?(\d+)\s+failed.*?(\d+)\s+errored", output)
    total_requests = int(req_m.group(1)) if req_m else 0
    succeeded = int(req_m.group(2)) if req_m else 0
    failed = int(req_m.group(3)) if req_m else 0
    errored = int(req_m.group(4)) if req_m else 0

    # request latency line:
    # request     :       18us      4.28ms        76us       102us       137us        66us        57us    98.10%
    # cols: min max median p95 p99 mean sd
    lat_m = re.search(r"request\s+:\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)", output)
    if lat_m:
        min_lat = parse_time_us(lat_m.group(1))
        max_lat = parse_time_us(lat_m.group(2))
        p50_lat = parse_time_us(lat_m.group(3))
        p95_lat = parse_time_us(lat_m.group(4))
        p99_lat = parse_time_us(lat_m.group(5))
        avg_lat = parse_time_us(lat_m.group(6))
    else:
        min_lat = max_lat = p50_lat = p95_lat = p99_lat = avg_lat = 0.0

    return {
        "total_requests": total_requests,
        "succeeded": succeeded,
        "failed": failed,
        "errored": errored,
        "rps": rps,
        "avg_lat_us": avg_lat,
        "p50_us": p50_lat,
        "p95_us": p95_lat,
        "p99_us": p99_lat,
    }

def run_benchmark_triplicate(framework, endpoint, duration="10s", concurrency="100", streams="10"):
    if framework not in FRAMEWORKS:
        print(f"Framework {framework} is UNSUPPORTED for HTTP/2.")
        return None

    fw = FRAMEWORKS[framework]
    url_path = ENDPOINTS[endpoint]
    port = fw["port"]
    url = f"https://127.0.0.1:{port}{url_path}"

    print("=" * 78)
    print(f" Triplicate Benchmark: {framework.upper()} (HTTP/2) on {url_path} (3 runs x {duration})")
    print(f" Connections: {concurrency} (Streams/Conn: {streams} -> Total Active: {int(concurrency)*int(streams)})")
    print(f" Server: Physical Cores 0, 1       (taskset -c 0,1, 2 worker threads)")
    print(f" Client: Physical Cores 2, 3, 4, 5 (taskset -c 2,3,4,5, 4 threads, h2load)")
    print("=" * 78)

    # Launch server pinned to cores 0, 1
    server_cmd = ["taskset", "-c", "0,1"] + fw["cmd"]
    proc = subprocess.Popen(server_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    try:
        # Wait for ready
        ready = False
        for _ in range(30):
            time.sleep(0.2)
            try:
                res = subprocess.run(["curl", "-k", "-s", "-f", f"https://127.0.0.1:{port}/plaintext"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if res.returncode == 0:
                    ready = True
                    break
            except Exception:
                pass

        if not ready:
            print(f"Error: Server failed to start on port {port}.")
            return None

        # Warmup (3s) with 4 client threads on cores 2,3,4,5
        print("==> [Warmup] Running 3s warmup via h2load...", flush=True)
        subprocess.run(
            ["taskset", "-c", "2,3,4,5", "h2load", "-t4", f"-c{concurrency}", f"-m{streams}", "-D3s", url],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        time.sleep(1)

        runs = []
        for i in range(1, 4):
            print(f"==> [Run {i}/3] Measuring ({duration})...", flush=True)
            res = subprocess.run(
                ["taskset", "-c", "2,3,4,5", "h2load", "-t4", f"-c{concurrency}", f"-m{streams}", f"-D{duration}", url],
                capture_output=True,
                text=True
            )
            data = parse_h2load_output(res.stdout)
            runs.append(data)
            print(f"    Run {i}: {data['rps']:,.2f} req/s | Mean: {data['avg_lat_us']:.2f} µs | p50: {data['p50_us']:.2f} µs | p99: {data['p99_us']:.2f} µs | Total: {data['total_requests']:,}")
            if i < 3:
                time.sleep(2) # 2s cooldown

        # Compute averages
        avg_rps = sum(r["rps"] for r in runs) / 3.0
        avg_lat = sum(r["avg_lat_us"] for r in runs) / 3.0
        avg_p50 = sum(r["p50_us"] for r in runs) / 3.0
        avg_p99 = sum(r["p99_us"] for r in runs) / 3.0
        avg_total = sum(r["total_requests"] for r in runs) / 3.0

        print("-" * 78)
        print(f" ⭐ {framework.upper()} (HTTP/2) 3-RUN AVERAGE on {url_path}:")
        print(f"    Throughput:   {avg_rps:,.2f} req/s")
        print(f"    Total Reqs:   {avg_total:,.0f} reqs")
        print(f"    Mean Latency: {avg_lat:.2f} µs")
        print(f"    p50 Latency:  {avg_p50:.2f} µs")
        print(f"    p99 Latency:  {avg_p99:.2f} µs")
        print("=" * 78)

        return {
            "framework": framework,
            "endpoint": endpoint,
            "runs": runs,
            "avg_rps": avg_rps,
            "avg_lat_us": avg_lat,
            "avg_p50_us": avg_p50,
            "avg_p99_us": avg_p99,
            "avg_total": avg_total,
        }

    finally:
        try:
            proc.terminate()
            proc.wait(timeout=2)
        except Exception:
            proc.kill()
        time.sleep(1)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 run_triplicate.py <aegon|actix|swerver> <plaintext|json|user_post> [duration] [concurrency] [streams]")
        sys.exit(1)
    
    fw = sys.argv[1]
    ep = sys.argv[2]
    dur = sys.argv[3] if len(sys.argv) > 3 else "10s"
    conc = sys.argv[4] if len(sys.argv) > 4 else "100"
    strm = sys.argv[5] if len(sys.argv) > 5 else "10"

    run_benchmark_triplicate(fw, ep, dur, conc, strm)
