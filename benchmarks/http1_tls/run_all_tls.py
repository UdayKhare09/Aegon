#!/usr/bin/env python3
import sys
import os
import json
import time
from run_triplicate import run_benchmark_triplicate

FRAMEWORKS = ["aegon", "swerver", "actix", "drogon", "fiber"]
ENDPOINTS = ["plaintext", "json", "user_post"]

def main():
    results = {}
    print("=" * 80)
    print(" STARTING COMPLETE HTTP/1.1 TLS BENCHMARK SUITE")
    print(" 5 Frameworks: Aegon, Swerver, Actix-web, Drogon, Fiber")
    print(" 3 Workloads: Plaintext, JSON, User Posts Route")
    print(" Protocol: HTTPS (HTTP/1.1 over TLS 1.3 / OpenSSL)")
    print(" Methodology: 3s warmup + 3 runs x 10s triplicate averages per workload")
    print(" Topology: 2 Server Cores (0, 1), 4 Client wrk Cores (2, 3, 4, 5), 100 conns")
    print("=" * 80)

    for ep in ENDPOINTS:
        results[ep] = {}
        print(f"\n>>>>>>> BENCHMARKING ENDPOINT: {ep.upper()} <<<<<<<")
        for fw in FRAMEWORKS:
            print(f"\n--- Framework: {fw.upper()} | Endpoint: {ep} ---")
            res = run_benchmark_triplicate(fw, ep, duration="10s", concurrency="100")
            results[ep][fw] = res
            time.sleep(3) # cooldown between frameworks

    # Save raw json results
    json_path = os.path.join(os.path.dirname(__file__), "raw_results.json")
    with open(json_path, "w") as f:
        json.dump(results, f, indent=2)

    print("\n" + "=" * 80)
    print(" ALL HTTP/1.1 TLS BENCHMARKS COMPLETE! RAW DATA SAVED TO:", json_path)
    print("=" * 80)

if __name__ == "__main__":
    main()
