#!/usr/bin/env python3
import subprocess
import time
import re
import json
import os
import sys

FRAMEWORKS = {
    "aegon": {
        "port": 18090,
        "cmd": ["benchmarks/appstack/servers/aegon/build/bench_aegon", "2", "18090"],
    },
    "actix": {
        "port": 18091,
        "cmd": ["benchmarks/appstack/servers/actix/target/release/bench_actix_appstack", "2", "18091"],
    },
    "swerver": {
        "port": 18094,
        "cmd": ["benchmarks/appstack/servers/swerver/zig-out/bin/bench_swerver", "2", "18094"],
    },
}

def parse_wrk_output(output):
    # Requests/sec: 423902.09
    rps_m = re.search(r"Requests/sec:\s+([0-9.]+)", output)
    rps = float(rps_m.group(1)) if rps_m else 0.0

    # Latency 235.12us 45.10us 2.10ms 89.12%
    lat_m = re.search(r"Latency\s+([0-9.]+[a-z]+)\s+([0-9.]+[a-z]+)\s+([0-9.]+[a-z]+)", output)
    avg_lat = lat_m.group(1) if lat_m else "0us"
    max_lat = lat_m.group(3) if lat_m else "0us"

    # Total requests
    req_m = re.search(r"([0-9]+)\s+requests in\s+([0-9.]+[a-z]+)", output)
    total_reqs = int(req_m.group(1)) if req_m else 0

    return {
        "rps": rps,
        "avg_lat": avg_lat,
        "max_lat": max_lat,
        "total_requests": total_reqs,
    }

def run_wrk(port, endpoint, method="GET", body=None, conns=512, duration="10s", warmup="2s"):
    url = f"http://127.0.0.1:{port}{endpoint}"
    
    # Optional lua script for POST with body
    lua_script = None
    if method == "POST" and body:
        lua_path = f"/tmp/post_{port}.lua"
        with open(lua_path, "w") as f:
            f.write(f'''
wrk.method = "POST"
wrk.body   = "{body}"
wrk.headers["Content-Type"] = "text/plain"
''')
        lua_script = lua_path

    # Warmup
    warmup_cmd = ["taskset", "-c", "2,3,4,5", "wrk", "-t", "4", "-c", str(min(conns, 256)), "-d", warmup, url]
    if lua_script:
        warmup_cmd += ["-s", lua_script]
    subprocess.run(warmup_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)

    # Measurement
    bench_cmd = ["taskset", "-c", "2,3,4,5", "wrk", "-t", "4", "-c", str(conns), "-d", duration, url]
    if lua_script:
        bench_cmd += ["-s", lua_script]
    res = subprocess.run(bench_cmd, capture_output=True, text=True)

    if lua_script and os.path.exists(lua_script):
        os.remove(lua_script)

    return parse_wrk_output(res.stdout)

def benchmark_framework(fw_name, endpoint, method="GET", body=None, conns=512, runs=3):
    fw = FRAMEWORKS[fw_name]
    port = fw["port"]

    # Start server pinned to physical cores 0, 1
    server_cmd = ["taskset", "-c", "0,1"] + fw["cmd"]
    proc = subprocess.Popen(server_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)

    try:
        run_results = []
        for r in range(runs):
            data = run_wrk(port, endpoint, method=method, body=body, conns=conns, duration="10s", warmup="2s")
            run_results.append(data)
            time.sleep(1)

        avg_rps = sum(d["rps"] for d in run_results) / len(run_results)
        return {
            "framework": fw_name,
            "conns": conns,
            "avg_rps": avg_rps,
            "runs": run_results,
        }
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        time.sleep(1)

def main():
    print("=" * 80)
    print(" HTTPARENA BENCHMARK SUITE: PROFILE 1 (BASELINE)")
    print(" Workloads: GET /baseline11?a=13&b=42 | POST /baseline11?a=13&b=42 (body=20)")
    print(" Concurrencies: 512 & 4096 connections")
    print(" Topology: 2 Server Cores (0,1), 4 Client Cores (2,3,4,5)")
    print(" Tool: wrk (4 threads, 10s duration x 3 triplicate runs)")
    print("=" * 80)

    summary = {}

    scenarios = [
        ("GET /baseline11 (512 conns)", "/baseline11?a=13&b=42", "GET", None, 512),
        ("GET /baseline11 (4096 conns)", "/baseline11?a=13&b=42", "GET", None, 4096),
        ("POST /baseline11 (512 conns)", "/baseline11?a=13&b=42", "POST", "20", 512),
        ("POST /baseline11 (4096 conns)", "/baseline11?a=13&b=42", "POST", "20", 4096),
    ]

    for title, ep, method, body, conns in scenarios:
        print(f"\n>>>>>>> SCENARIO: {title} <<<<<<<")
        summary[title] = {}
        for fw in ["aegon", "actix", "swerver"]:
            res = benchmark_framework(fw, ep, method=method, body=body, conns=conns, runs=3)
            summary[title][fw] = res
            print(f"  {fw.upper():<10}: {res['avg_rps']:>12,.2f} req/s (Runs: {[round(r['rps'], 1) for r in res['runs']]})")

    # Save raw json
    json_path = "benchmarks/appstack/baseline_results.json"
    with open(json_path, "w") as f:
        json.dump(summary, f, indent=2)

    print("\n" + "=" * 80)
    print(f" BASELINE BENCHMARKS COMPLETE! Results saved to {json_path}")
    print("=" * 80)

if __name__ == "__main__":
    main()
