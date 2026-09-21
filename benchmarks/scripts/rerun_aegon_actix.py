#!/usr/bin/env python3
import sys
import os
import json
import time
import importlib.util

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# Load http1_tls runner
spec_tls = importlib.util.spec_from_file_location("tls_runner", os.path.join(REPO_ROOT, "benchmarks", "http1_tls", "run_triplicate.py"))
tls_runner = importlib.util.module_from_spec(spec_tls)
spec_tls.loader.exec_module(tls_runner)

# Load http2 runner
spec_h2 = importlib.util.spec_from_file_location("h2_runner", os.path.join(REPO_ROOT, "benchmarks", "http2", "run_triplicate.py"))
h2_runner = importlib.util.module_from_spec(spec_h2)
spec_h2.loader.exec_module(h2_runner)

FRAMEWORKS = ["aegon", "actix"]
ENDPOINTS = ["plaintext", "json", "user_post"]

def rerun_http1_tls():
    print("=" * 80)
    print(">>> RUNNING HTTP/1.1 TLS BENCHMARKS (AEGON & ACTIX WITH RUSTLS) <<<")
    print("=" * 80, flush=True)
    
    json_path = os.path.join(REPO_ROOT, "benchmarks", "http1_tls", "raw_results.json")
    with open(json_path, "r") as f:
        data = json.load(f)

    # Store old actix results for comparison
    old_actix = {ep: data.get(ep, {}).get("actix", {}) for ep in ENDPOINTS}

    tls_results = {}
    for ep in ENDPOINTS:
        print(f"\n==================== HTTP/1.1 TLS: {ep.upper()} ====================", flush=True)
        tls_results[ep] = {}
        for fw in FRAMEWORKS:
            print(f"\n---> Running {fw.upper()} on {ep}...", flush=True)
            res = tls_runner.run_benchmark_triplicate(fw, ep, duration="10s", concurrency="100")
            tls_results[ep][fw] = res
            if data.get(ep) is not None:
                data[ep][fw] = res
            time.sleep(3)

    # Save updated json
    with open(json_path, "w") as f:
        json.dump(data, f, indent=2)
    print(f"\n[+] Updated {json_path}", flush=True)
    return old_actix, tls_results

def rerun_http2():
    print("\n" + "=" * 80)
    print(">>> RUNNING HTTP/2 BENCHMARKS (AEGON & ACTIX WITH RUSTLS) <<<")
    print("=" * 80, flush=True)
    
    json_path = os.path.join(REPO_ROOT, "benchmarks", "http2", "raw_results.json")
    with open(json_path, "r") as f:
        data = json.load(f)

    # Store old actix results for comparison
    old_actix = {ep: data.get(ep, {}).get("actix", {}) for ep in ENDPOINTS}

    h2_results = {}
    for ep in ENDPOINTS:
        print(f"\n==================== HTTP/2: {ep.upper()} ====================", flush=True)
        h2_results[ep] = {}
        for fw in FRAMEWORKS:
            print(f"\n---> Running {fw.upper()} on {ep}...", flush=True)
            res = h2_runner.run_benchmark_triplicate(fw, ep, duration="10s", concurrency="100", streams="10")
            h2_results[ep][fw] = res
            if data.get(ep) is not None:
                data[ep][fw] = res
            time.sleep(3)

    # Save updated json
    with open(json_path, "w") as f:
        json.dump(data, f, indent=2)
    print(f"\n[+] Updated {json_path}", flush=True)
    return old_actix, h2_results

if __name__ == "__main__":
    os.chdir(REPO_ROOT)
    old_tls_actix, new_tls = rerun_http1_tls()
    old_h2_actix, new_h2 = rerun_http2()

    summary_file = os.path.join(REPO_ROOT, "benchmarks", "rerun_summary.json")
    with open(summary_file, "w") as f:
        json.dump({
            "old_tls_actix": old_tls_actix,
            "new_tls": new_tls,
            "old_h2_actix": old_h2_actix,
            "new_h2": new_h2,
        }, f, indent=2)
    print("\n[+] Full comparison summary saved to", summary_file, flush=True)
