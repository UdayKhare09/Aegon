#!/usr/bin/env python3
import sys
import os
import time
import subprocess
import re
import signal

# Topology: 2 Cores for Server (0,1), 4 Cores for wrk Client (2,3,4,5)
FRAMEWORKS = {
    "aegon": {
        "port": 18080,
        "cmd": ["benchmarks/http1_plain/build/aegon/bench_aegon", "2", "18080"],
    },
    "actix": {
        "port": 18081,
        "cmd": ["benchmarks/http1_plain/servers/actix/target/release/bench_actix", "2", "18081"],
    },
    "fiber": {
        "port": 18082,
        "cmd": ["benchmarks/http1_plain/servers/fiber/bench_fiber", "2", "18082"],
    },
    "drogon": {
        "port": 18083,
        "cmd": ["benchmarks/http1_plain/build/drogon/bench_drogon", "2", "18083"],
    },
    "swerver": {
        "port": 18084,
        "cmd": ["benchmarks/http1_plain/servers/swerver/zig-out/bin/bench_swerver", "2", "18084"],
    },
}

ENDPOINTS = {
    "plaintext": "/plaintext",
    "json": "/json",
    "user_post": "/users/42/posts/101",
    "users": "/users/42/posts/101",
}

def parse_time_us(val_str, unit):
    val = float(val_str)
    if unit == "ms":
        return val * 1000.0
    elif unit == "s":
        return val * 1000000.0
    return val

def parse_wrk_output(output):
    # Total requests & duration
    req_match = re.search(r"(\d+)\s+requests\s+in\s+([0-9.]+)s", output)
    total_requests = int(req_match.group(1)) if req_match else 0
    duration_s = float(req_match.group(2)) if req_match else 10.0

    # Requests/sec
    rps_match = re.search(r"Requests/sec:\s+([0-9.]+)", output)
    rps = float(rps_match.group(1)) if rps_match else 0.0

    # Avg Latency: "Latency   135.34us"
    avg_lat_match = re.search(r"Latency\s+([0-9.]+)(us|ms|s)", output)
    avg_latency = parse_time_us(avg_lat_match.group(1), avg_lat_match.group(2)) if avg_lat_match else 0.0

    # p50 & p99
    p50_match = re.search(r"50%\s+([0-9.]+)(us|ms|s)", output)
    p50 = parse_time_us(p50_match.group(1), p50_match.group(2)) if p50_match else 0.0

    p99_match = re.search(r"99%\s+([0-9.]+)(us|ms|s)", output)
    p99 = parse_time_us(p99_match.group(1), p99_match.group(2)) if p99_match else 0.0

    return {
        "total_requests": total_requests,
        "duration_s": duration_s,
        "rps": rps,
        "avg_lat_us": avg_latency,
        "p50_us": p50,
        "p99_us": p99,
    }

def run_benchmark_triplicate(framework, endpoint, duration="10s", concurrency="100"):
    fw = FRAMEWORKS[framework]
    url_path = ENDPOINTS[endpoint]
    port = fw["port"]
    url = f"http://127.0.0.1:{port}{url_path}"

    print("=" * 78)
    print(f" Triplicate Benchmark: {framework.upper()} on {url_path} (3 runs x {duration})")
    print(f" Server: Physical Cores 0, 1       (taskset -c 0,1, 2 worker threads)")
    print(f" Client: Physical Cores 2, 3, 4, 5 (taskset -c 2,3,4,5, 4 threads, {concurrency} conn)")
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
                res = subprocess.run(["curl", "-s", "-f", f"http://127.0.0.1:{port}/plaintext"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if res.returncode == 0:
                    ready = True
                    break
            except Exception:
                pass

        if not ready:
            print(f"Error: Server failed to start on port {port}.")
            return None

        # Warmup (3s) with 4 client threads on cores 2,3,4,5
        print("==> [Warmup] Running 3s warmup...")
        subprocess.run(["taskset", "-c", "2,3,4,5", "wrk", "-t4", f"-c{concurrency}", "-d3s", url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1)

        runs = []
        for i in range(1, 4):
            print(f"==> [Run {i}/3] Measuring ({duration})...", flush=True)
            res = subprocess.run(
                ["taskset", "-c", "2,3,4,5", "wrk", "-t4", f"-c{concurrency}", f"-d{duration}", "--latency", url],
                capture_output=True,
                text=True
            )
            data = parse_wrk_output(res.stdout)
            runs.append(data)
            print(f"    Run {i}: {data['rps']:,.2f} req/s | Avg: {data['avg_lat_us']:.2f} µs | p50: {data['p50_us']:.2f} µs | p99: {data['p99_us']:.2f} µs | Total: {data['total_requests']:,}")
            if i < 3:
                time.sleep(2) # 2s cooldown

        # Compute averages
        avg_rps = sum(r["rps"] for r in runs) / 3.0
        avg_lat = sum(r["avg_lat_us"] for r in runs) / 3.0
        avg_p50 = sum(r["p50_us"] for r in runs) / 3.0
        avg_p99 = sum(r["p99_us"] for r in runs) / 3.0
        avg_total = sum(r["total_requests"] for r in runs) / 3.0

        print("-" * 78)
        print(f" ⭐ {framework.upper()} 3-RUN AVERAGE on {url_path}:")
        print(f"    Throughput:   {avg_rps:,.2f} req/s")
        print(f"    Total Reqs:   {avg_total:,.0f} reqs")
        print(f"    Avg Latency:  {avg_lat:.2f} µs")
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
        # Cleanup server
        try:
            proc.terminate()
            proc.wait(timeout=2)
        except Exception:
            proc.kill()
        if framework == "fiber":
            subprocess.run(["pkill", "-f", "bench_fiber"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 run_triplicate.py <aegon|actix|fiber|drogon> <plaintext|json|user_post> [duration] [concurrency]")
        sys.exit(1)
    
    fw = sys.argv[1]
    ep = sys.argv[2]
    dur = sys.argv[3] if len(sys.argv) > 3 else "10s"
    conc = sys.argv[4] if len(sys.argv) > 4 else "100"

    run_benchmark_triplicate(fw, ep, dur, conc)
