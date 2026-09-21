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
        "supported": True,
    },
    "actix": {
        "port": 18091,
        "cmd": ["benchmarks/appstack/servers/actix/target/release/bench_actix_appstack", "2", "18091"],
        "supported": True,
    },
    "swerver": {
        "port": 18094,
        "cmd": ["benchmarks/appstack/servers/swerver/zig-out/bin/bench_swerver", "2", "18094"],
        "supported": True,
    },
}

def parse_wrk_output(output):
    rps_m = re.search(r"Requests/sec:\s+([0-9.]+)", output)
    rps = float(rps_m.group(1)) if rps_m else 0.0

    lat_m = re.search(r"Latency\s+([0-9.]+[a-z]+)\s+([0-9.]+[a-z]+)\s+([0-9.]+[a-z]+)", output)
    avg_lat = lat_m.group(1) if lat_m else "0us"
    max_lat = lat_m.group(3) if lat_m else "0us"

    req_m = re.search(r"([0-9]+)\s+requests in\s+([0-9.]+[a-z]+)", output)
    total_reqs = int(req_m.group(1)) if req_m else 0

    return {
        "rps": rps,
        "avg_lat": avg_lat,
        "max_lat": max_lat,
        "total_requests": total_reqs,
    }

def run_wrk(port, endpoint, conns=512, gzip=False, duration="10s", warmup="2s"):
    url = f"http://127.0.0.1:{port}{endpoint}"

    extra_headers = ["-H", "Accept-Encoding: gzip"] if gzip else []

    # Warmup
    warmup_cmd = ["taskset", "-c", "2,3,4,5", "wrk", "-t", "4", "-c", str(min(conns, 256)), "-d", warmup] + extra_headers + [url]
    subprocess.run(warmup_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)

    # Measurement
    bench_cmd = ["taskset", "-c", "2,3,4,5", "wrk", "-t", "4", "-c", str(conns), "-d", duration] + extra_headers + [url]
    res = subprocess.run(bench_cmd, capture_output=True, text=True)

    return parse_wrk_output(res.stdout)

def benchmark_framework(fw_name, endpoint, conns=512, gzip=False, runs=3):
    fw = FRAMEWORKS[fw_name]
    if not fw["supported"]:
        return None

    # Kill any stale server processes
    subprocess.run(["killall", "-9", os.path.basename(fw["cmd"][0])], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
    time.sleep(0.5)

    env = os.environ.copy()
    env["DATASET_PATH"] = "benchmarks/appstack/data/dataset.json"

    # Start server pinned to cores 0, 1
    server_cmd = ["taskset", "-c", "0,1"] + fw["cmd"]
    proc = subprocess.Popen(server_cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)

    try:
        run_results = []
        for r in range(runs):
            data = run_wrk(fw["port"], endpoint, conns=conns, gzip=gzip)
            run_results.append(data)
            label = f"{fw_name.upper()} Run {r+1}/{runs}"
            print(f"    {label:<18}: {data['rps']:>10.2f} req/s | avg lat: {data['avg_lat']:>7} | max: {data['max_lat']:>7}")
            time.sleep(1)

        avg_rps = sum(x["rps"] for x in run_results) / len(run_results)
        return {
            "framework": fw_name,
            "conns": conns,
            "gzip": gzip,
            "avg_rps": avg_rps,
            "runs": run_results,
        }
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
        time.sleep(0.5)

def main():
    conns_list = [512, 4096]
    test_frameworks = ["aegon", "actix", "swerver"]
    if len(sys.argv) > 1:
        test_frameworks = [sys.argv[1]]

    scenarios = [
        ("/json/50?m=1", False, "Plain JSON"),
        ("/json/50?m=1", True, "Gzip Compressed JSON"),
    ]

    all_results = {}
    if os.path.exists("benchmarks/appstack/json_results.json"):
        try:
            with open("benchmarks/appstack/json_results.json", "r") as f:
                all_results = json.load(f)
        except Exception:
            pass

    for endpoint, gzip, desc in scenarios:
        for conns in conns_list:
            tag = f"{desc} ({conns} conns)"
            print(f"\n=======================================================")
            print(f" BENCHMARK: {tag} ({endpoint})")
            print(f"=======================================================")
            if tag not in all_results:
                all_results[tag] = {}

            for fw in test_frameworks:
                print(f"\n>>> Running {fw.upper()} ({conns} connections, gzip={gzip})...")
                res = benchmark_framework(fw, endpoint, conns=conns, gzip=gzip, runs=3)
                if res:
                    all_results[tag][fw] = res
                    print(f"  --> {fw.upper()} AVERAGE: {res['avg_rps']:>10.2f} req/s")
                else:
                    print(f"  --> {fw.upper()} UNSUPPORTED")

            # Save incrementally
            with open("benchmarks/appstack/json_results.json", "w") as f:
                json.dump(all_results, f, indent=2)

    print("\n\nAll JSON benchmark runs complete! Results saved to benchmarks/appstack/json_results.json")

if __name__ == "__main__":
    main()
