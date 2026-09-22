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
        "env": {"PG_CONNINFO": "host=127.0.0.1 port=5432 user=bench password=bench dbname=benchmark"},
        "supported": True,
    },
    "actix": {
        "port": 18091,
        "cmd": ["benchmarks/appstack/servers/actix/target/release/bench_actix_appstack", "2", "18091"],
        "env": {"DATABASE_URL": "postgres://bench:bench@127.0.0.1:5432/benchmark"},
        "supported": True,
    },
    "swerver": {
        "port": 18094,
        "cmd": ["benchmarks/appstack/servers/swerver/zig-out/bin/bench_swerver", "--config", "benchmarks/appstack/servers/swerver/config.json"],
        "env": {"PGPASSWORD": "bench"},
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

    err_m = re.search(r"Non-2xx or 3xx responses:\s+([0-9]+)", output)
    errors = int(err_m.group(1)) if err_m else 0

    valid_rps = rps * (1.0 - (errors / total_reqs)) if total_reqs > 0 else 0.0

    return {
        "rps": rps,
        "valid_rps": valid_rps,
        "errors": errors,
        "avg_lat": avg_lat,
        "max_lat": max_lat,
        "total_requests": total_reqs,
    }

def run_wrk(port, endpoint, conns=512, duration="10s", warmup="3s"):
    url = f"http://127.0.0.1:{port}{endpoint}"

    # Warmup
    warmup_cmd = ["taskset", "-c", "2,3,4,5", "wrk", "-t", "4", "-c", str(min(conns, 256)), "-d", warmup, url]
    subprocess.run(warmup_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)

    # Measurement
    bench_cmd = ["taskset", "-c", "2,3,4,5", "wrk", "-t", "4", "-c", str(conns), "-d", duration, url]
    res = subprocess.run(bench_cmd, capture_output=True, text=True)

    if res.returncode != 0 or "Requests/sec" not in res.stdout:
        print(f"  [WRK WARNING] returncode={res.returncode}, stderr={res.stderr.strip()[:200]}")

    return parse_wrk_output(res.stdout)

def is_server_ready(port, endpoint="/async-db?min=10&max=50&limit=5"):
    try:
        res = subprocess.run(
            ["curl", "-s", "-o", "/dev/null", "-w", "%{http_code}", f"http://127.0.0.1:{port}{endpoint}"],
            capture_output=True, text=True, timeout=2
        )
        return res.stdout.strip() == "200"
    except Exception:
        return False

def wait_for_server(port, timeout=10):
    start = time.time()
    while time.time() - start < timeout:
        if is_server_ready(port):
            return True
        time.sleep(0.2)
    return False

def kill_process_tree(pid):
    try:
        subprocess.run(["pkill", "-9", "-P", str(pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.kill(pid, 9)
    except Exception:
        pass

def main():
    conns_list = [512, 1024]
    test_frameworks = ["aegon", "actix", "swerver"]
    if len(sys.argv) > 1:
        test_frameworks = [sys.argv[1]]

    endpoint = "/async-db?min=10&max=50&limit=50"
    results = {}
    if os.path.exists("benchmarks/appstack/db_results.json"):
        try:
            with open("benchmarks/appstack/db_results.json", "r") as f:
                results = json.load(f)
        except Exception:
            pass

    for fw in test_frameworks:
        meta = FRAMEWORKS[fw]
        if not meta["supported"]:
            continue

        print(f"\n========================================================")
        print(f"  Starting Framework: {fw.upper()} on port {meta['port']}")
        print(f"========================================================")

        run_env = os.environ.copy()
        if "env" in meta:
            run_env.update(meta["env"])

        cmd = ["taskset", "-c", "0,1"] + meta["cmd"]
        proc = subprocess.Popen(cmd, env=run_env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        if not wait_for_server(meta["port"]):
            print(f"Failed to start {fw} on port {meta['port']}")
            kill_process_tree(proc.pid)
            continue

        results[fw] = {}

        for conns in conns_list:
            print(f"\n--- Testing {fw.upper()} | Concurrency: {conns} | Endpoint: {endpoint} ---")

            runs = []
            for r in range(3):
                metrics = run_wrk(meta["port"], endpoint, conns=conns, duration="10s", warmup="3s")
                runs.append(metrics)
                err_str = f" | Errors: {metrics['errors']}" if metrics['errors'] > 0 else ""
                print(f"  Run {r+1}: {metrics['valid_rps']:,.2f} req/s{err_str} | Avg Lat: {metrics['avg_lat']} | Max Lat: {metrics['max_lat']}")
                time.sleep(2)

            avg_valid_rps = sum(x["valid_rps"] for x in runs) / len(runs)
            total_errors = sum(x["errors"] for x in runs)
            err_summary = f" (Total Errors: {total_errors})" if total_errors > 0 else ""
            print(f"==> Triplicate Average: {avg_valid_rps:,.2f} valid req/s{err_summary}")

            results[fw][str(conns)] = {
                "avg_rps": avg_valid_rps,
                "total_errors": total_errors,
                "runs": runs,
            }

        kill_process_tree(proc.pid)
        try:
            proc.kill()
        except Exception:
            pass
        time.sleep(1)

    print("\n\n========================================================")
    print("           FINAL ASYNC-DB BENCHMARK SCORECARD           ")
    print("========================================================")
    print(f"{'Framework':<12} | {'512 Conns (req/s)':<20} | {'1,024 Conns (req/s)':<20}")
    print("-" * 60)

    for fw in test_frameworks:
        if fw in results:
            r512 = f"{results[fw].get('512', {}).get('avg_rps', 0.0):,.2f}"
            r1024 = f"{results[fw].get('1024', {}).get('avg_rps', 0.0):,.2f}"
            print(f"{fw.capitalize():<12} | {r512:<20} | {r1024:<20}")

    with open("benchmarks/appstack/db_results.json", "w") as f:
        json.dump(results, f, indent=2)
    print("\nSaved detailed benchmark data to benchmarks/appstack/db_results.json")

if __name__ == "__main__":
    main()
