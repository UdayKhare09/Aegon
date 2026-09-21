#!/usr/bin/env python3
import sys
import os
import json
import time
from run_triplicate import run_benchmark_triplicate

FRAMEWORKS = ["aegon", "swerver", "actix"]
ENDPOINTS = ["plaintext", "json", "user_post"]

def main():
    results = {}
    print("=" * 80)
    print(" STARTING COMPLETE HTTP/2 BENCHMARK SUITE")
    print(" 3 Active Frameworks: Aegon (C++26), Swerver (Zig), Actix-web (Rust)")
    print(" 2 Unsupported Frameworks: Drogon (C++), Fiber (Go) -> No RFC 7540 HTTP/2")
    print(" 3 Workloads: Plaintext, JSON, User Posts Route")
    print(" Protocol: HTTPS (HTTP/2 over TLS 1.3 / ALPN h2)")
    print(" Tool: h2load (nghttp2 v1.70.0)")
    print(" Methodology: 3s warmup + 3 runs x 10s triplicate averages per workload")
    print(" Topology: 2 Server Cores (0, 1), 4 Client Cores (2, 3, 4, 5)")
    print(" Multiplexing: 100 connections x 10 streams = 1,000 active concurrent streams")
    print("=" * 80)

    for ep in ENDPOINTS:
        results[ep] = {}
        print(f"\n>>>>>>> BENCHMARKING ENDPOINT: {ep.upper()} <<<<<<<")
        for fw in FRAMEWORKS:
            print(f"\n--- Framework: {fw.upper()} | Endpoint: {ep} ---")
            res = run_benchmark_triplicate(fw, ep, duration="10s", concurrency="100", streams="10")
            results[ep][fw] = res
            time.sleep(3) # cooldown between frameworks

    # Add unsupported entries for transparency
    for ep in ENDPOINTS:
        results[ep]["drogon"] = {
            "framework": "drogon",
            "endpoint": ep,
            "status": "UNSUPPORTED",
            "reason": "Trantor reactor does not implement RFC 7540 HTTP/2 binary framing; ALPN falls back to HTTP/1.1."
        }
        results[ep]["fiber"] = {
            "framework": "fiber",
            "endpoint": ep,
            "status": "UNSUPPORTED",
            "reason": "fasthttp core does not implement RFC 7540 HTTP/2 binary framing; ALPN falls back to HTTP/1.1."
        }

    # Save raw json results
    json_path = os.path.join(os.path.dirname(__file__), "raw_results.json")
    with open(json_path, "w") as f:
        json.dump(results, f, indent=2)

    print("\n" + "=" * 80)
    print(" ALL HTTP/2 BENCHMARKS COMPLETE! RAW DATA SAVED TO:", json_path)
    print("=" * 80)

if __name__ == "__main__":
    main()
