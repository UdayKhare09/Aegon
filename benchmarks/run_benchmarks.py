#!/usr/bin/env python3
"""
Unified HttpArena-Compliant Benchmark Runner
Benchmarking Aegon vs Viltrum across H1 Plain, H1 TLS, H2, and WebSocket.

Usage:
    python3 benchmarks/run_benchmarks.py [options]

Examples:
    # Run all protocols and workloads in single run mode
    python3 benchmarks/run_benchmarks.py

    # Run triplicate (3 runs with average & stddev)
    python3 benchmarks/run_benchmarks.py --runs 3

    # Benchmark only WebSocket
    python3 benchmarks/run_benchmarks.py --protocols ws

    # Benchmark HTTP/2 baseline and json workloads on Aegon only
    python3 benchmarks/run_benchmarks.py -p h2 -w baseline,json -s aegon

    # Quick 3s test with 50 connections
    python3 benchmarks/run_benchmarks.py -d 3 -c 50
"""

import sys
import os
import time
import subprocess
import argparse
import json
import re
import signal
import shutil
from typing import Dict, List, Optional, Any

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SCRIPT_DIR)

AEGON_BIN = os.path.join(ROOT_DIR, "build", "benchmarks", "bench_aegon")
if not os.path.exists(AEGON_BIN):
    AEGON_BIN = os.path.join(SCRIPT_DIR, "servers", "aegon", "bench_aegon")

VILTRUM_BIN = os.path.join(SCRIPT_DIR, "servers", "viltrum", "bench_viltrum")

# Tool discovery
WRK_BIN = shutil.which("wrk") or "/usr/bin/wrk"
H2LOAD_BIN = shutil.which("h2load") or "/usr/bin/h2load"
GCANNON_BIN = shutil.which("gcannon") or os.path.expanduser("~/.local/bin/gcannon")

SERVERS_CONFIG = {
    "aegon": {
        "name": "Aegon",
        "bin": AEGON_BIN,
        "base_port": 18080,
        "ports": {
            "h1_plain": 18080,
            "h1_tls": 18081,
            "h2": 18082,
            "ws": 18080,
        },
    },
    "viltrum": {
        "name": "Viltrum",
        "bin": VILTRUM_BIN,
        "base_port": 18085,
        "ports": {
            "h1_plain": 18085,
            "h1_tls": 18086,
            "h2": 18087,
            "ws": 18085,
        },
    },
}

HTTP_WORKLOADS = {
    "baseline": {
        "method": "GET",
        "path": "/baseline?a=13&b=42",
        "desc": "Arithmetic & Param Parse",
    },
    "pipeline": {
        "method": "GET",
        "path": "/pipeline",
        "desc": "Zero-Payload Throughput",
    },
    "json": {
        "method": "GET",
        "path": "/json/10?m=2",
        "desc": "Dynamic JSON Serialization",
    },
    "delay": {
        "method": "GET",
        "path": "/delay/10",
        "desc": "10ms Async EventLoop Timer",
    },
    "echo": {
        "method": "POST",
        "path": "/echo",
        "desc": "Stream & Body Handling",
    },
}

WS_WORKLOADS = {
    "echo": {
        "path": "/ws",
        "pipeline": 1,
        "req_per_conn": 0,
        "desc": "WebSocket 1-in-flight Ping-Pong",
    },
    "pipeline": {
        "path": "/ws",
        "pipeline": 16,
        "req_per_conn": 0,
        "desc": "WebSocket 16-in-flight Pipeline",
    },
    "limited-conn": {
        "path": "/ws",
        "pipeline": 1,
        "req_per_conn": 10,
        "desc": "WebSocket Reconnect (10 frames/conn)",
    },
}

def parse_time_us(val_str: str, unit: str) -> float:
    try:
        val = float(val_str)
        unit = unit.lower()
        if unit == "ms":
            return val * 1000.0
        elif unit == "s":
            return val * 1000000.0
        elif unit in ("ns",):
            return val / 1000.0
        return val
    except Exception:
        return 0.0

def parse_wrk_output(output: str) -> Dict[str, Any]:
    req_match = re.search(r"(\d+)\s+requests\s+in\s+([0-9.]+)s", output)
    total_requests = int(req_match.group(1)) if req_match else 0
    duration_s = float(req_match.group(2)) if req_match else 10.0

    rps_match = re.search(r"Requests/sec:\s+([0-9.]+)", output)
    rps = float(rps_match.group(1)) if rps_match else 0.0

    transfer_match = re.search(r"Transfer/sec:\s+([0-9.]+)(KB|MB|GB)", output)
    transfer_mb_s = 0.0
    if transfer_match:
        val = float(transfer_match.group(1))
        u = transfer_match.group(2)
        transfer_mb_s = val / 1024.0 if u == "KB" else (val if u == "MB" else val * 1024.0)

    avg_lat_match = re.search(r"Latency\s+([0-9.]+)(us|ms|s)", output)
    avg_latency = parse_time_us(avg_lat_match.group(1), avg_lat_match.group(2)) if avg_lat_match else 0.0

    p50_match = re.search(r"50%\s+([0-9.]+)(us|ms|s)", output)
    p50 = parse_time_us(p50_match.group(1), p50_match.group(2)) if p50_match else 0.0

    p90_match = re.search(r"90%\s+([0-9.]+)(us|ms|s)", output)
    p90 = parse_time_us(p90_match.group(1), p90_match.group(2)) if p90_match else 0.0

    p99_match = re.search(r"99%\s+([0-9.]+)(us|ms|s)", output)
    p99 = parse_time_us(p99_match.group(1), p99_match.group(2)) if p99_match else 0.0

    return {
        "requests": total_requests,
        "duration_s": duration_s,
        "rps": rps,
        "mb_s": transfer_mb_s,
        "avg_us": avg_latency,
        "p50_us": p50,
        "p90_us": p90,
        "p99_us": p99,
    }

def parse_h2load_output(output: str) -> Dict[str, Any]:
    rps_match = re.search(r"([0-9.]+)\s+req/s", output)
    rps = float(rps_match.group(1)) if rps_match else 0.0

    traffic_match = re.search(r"([0-9.]+)(KB|MB|GB)/s", output)
    mb_s = 0.0
    if traffic_match:
        val = float(traffic_match.group(1))
        u = traffic_match.group(2)
        mb_s = val / 1024.0 if u == "KB" else (val if u == "MB" else val * 1024.0)

    total_match = re.search(r"requests:\s+(\d+)\s+total", output)
    total_requests = int(total_match.group(1)) if total_match else 0

    dur_match = re.search(r"finished in\s+([0-9.]+)s", output)
    duration_s = float(dur_match.group(1)) if dur_match else 10.0

    # request latency row: min max median p95 p99 mean sd
    req_row = re.search(r"request\s*:\s*([0-9.]+\w+)\s+([0-9.]+\w+)\s+([0-9.]+\w+)\s+([0-9.]+\w+)\s+([0-9.]+\w+)\s+([0-9.]+\w+)", output)
    avg_us, p50_us, p90_us, p99_us = 0.0, 0.0, 0.0, 0.0
    if req_row:
        p50_raw = req_row.group(3)
        p95_raw = req_row.group(4)
        p99_raw = req_row.group(5)
        mean_raw = req_row.group(6)

        def to_us(s: str) -> float:
            m = re.match(r"([0-9.]+)(\w+)", s)
            return parse_time_us(m.group(1), m.group(2)) if m else 0.0

        p50_us = to_us(p50_raw)
        p90_us = to_us(p95_raw) # h2load outputs p95
        p99_us = to_us(p99_raw)
        avg_us = to_us(mean_raw)

    return {
        "requests": total_requests,
        "duration_s": duration_s,
        "rps": rps,
        "mb_s": mb_s,
        "avg_us": avg_us,
        "p50_us": p50_us,
        "p90_us": p90_us,
        "p99_us": p99_us,
    }

def parse_gcannon_json(output: str) -> Dict[str, Any]:
    try:
        data = json.loads(output)
        lat = data.get("latency_us", {})
        return {
            "requests": data.get("requests", 0),
            "duration_s": data.get("elapsed_sec", 10.0),
            "rps": data.get("rps", 0.0),
            "mb_s": (data.get("bandwidth_bps", 0.0) / 1024.0 / 1024.0),
            "avg_us": float(lat.get("avg", 0.0)),
            "p50_us": float(lat.get("p50", 0.0)),
            "p90_us": float(lat.get("p90", 0.0)),
            "p99_us": float(lat.get("p99", 0.0)),
        }
    except Exception as e:
        print(f"[Warning] Failed to parse gcannon json output: {e}\n{output[:200]}")
        return {
            "requests": 0, "duration_s": 0.0, "rps": 0.0, "mb_s": 0.0,
            "avg_us": 0.0, "p50_us": 0.0, "p90_us": 0.0, "p99_us": 0.0,
        }

class BenchmarkRunner:
    def __init__(self, args):
        self.args = args
        self.active_processes: Dict[str, subprocess.Popen] = {}
        self.results: Dict[str, Any] = {}

    def is_port_open(self, port: int) -> bool:
        cmd = ["curl", "-s", "-f", "-m", "1", f"http://127.0.0.1:{port}/baseline?a=1&b=2"]
        res = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return res.returncode == 0

    def start_server(self, server_key: str) -> bool:
        cfg = SERVERS_CONFIG[server_key]
        base_port = cfg["base_port"]

        if self.is_port_open(base_port):
            print(f"  [Server] {cfg['name']} is already running on port {base_port}.")
            return True

        if not os.path.exists(cfg["bin"]):
            print(f"  [Error] Binary for {cfg['name']} not found at {cfg['bin']}.")
            return False

        cert_file = os.path.join(SCRIPT_DIR, "certs", "server.crt")
        key_file = os.path.join(SCRIPT_DIR, "certs", "server.key")

        cmd = [
            "taskset", "-c", self.args.server_cores,
            cfg["bin"],
            "2",
            str(base_port),
            cert_file,
            key_file,
        ]

        print(f"  [Server] Launching {cfg['name']} on Cores {self.args.server_cores} (base port {base_port})...")
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.active_processes[server_key] = proc

        # Wait for health check
        for _ in range(40):
            time.sleep(0.15)
            if self.is_port_open(base_port):
                time.sleep(0.2)
                return True

        print(f"  [Error] {cfg['name']} failed to respond on port {base_port}.")
        self.stop_server(server_key)
        return False

    def stop_server(self, server_key: str):
        if server_key in self.active_processes:
            proc = self.active_processes[server_key]
            try:
                proc.terminate()
                proc.wait(timeout=2)
            except Exception:
                proc.kill()
            del self.active_processes[server_key]
            time.sleep(0.3)

    def stop_all(self):
        for k in list(self.active_processes.keys()):
            self.stop_server(k)

    def execute_test(self, server_key: str, protocol: str, workload_name: str, duration: int) -> Dict[str, Any]:
        cfg = SERVERS_CONFIG[server_key]
        port = cfg["ports"][protocol]
        cores = self.args.client_cores
        conns = self.args.concurrency
        threads = self.args.threads

        lua_echo = os.path.join(SCRIPT_DIR, "requests", "post_echo.lua")
        echo_bin = os.path.join(SCRIPT_DIR, "requests", "echo_payload.bin")

        if protocol == "h1_plain":
            wl = HTTP_WORKLOADS[workload_name]
            url = f"http://127.0.0.1:{port}{wl['path']}"
            cmd = ["taskset", "-c", cores, WRK_BIN, f"-t{threads}", f"-c{conns}", f"-d{duration}s", "--latency"]
            if workload_name == "echo":
                cmd += ["-s", lua_echo]
            cmd.append(url)
            res = subprocess.run(cmd, capture_output=True, text=True)
            return parse_wrk_output(res.stdout)

        elif protocol == "h1_tls":
            wl = HTTP_WORKLOADS[workload_name]
            url = f"https://127.0.0.1:{port}{wl['path']}"
            cmd = ["taskset", "-c", cores, WRK_BIN, f"-t{threads}", f"-c{conns}", f"-d{duration}s", "--latency"]
            if workload_name == "echo":
                cmd += ["-s", lua_echo]
            cmd.append(url)
            res = subprocess.run(cmd, capture_output=True, text=True)
            return parse_wrk_output(res.stdout)

        elif protocol == "h2":
            wl = HTTP_WORKLOADS[workload_name]
            url = f"https://127.0.0.1:{port}{wl['path']}"
            cmd = [
                "taskset", "-c", cores,
                H2LOAD_BIN,
                "-n", "0",
                "-D", f"{duration}s",
                "-c", str(conns),
                "-m", "16",
                "-t", str(threads),
            ]
            if workload_name == "echo":
                cmd += ["-d", echo_bin, "-H", "Content-Type: application/octet-stream"]
            cmd.append(url)
            res = subprocess.run(cmd, capture_output=True, text=True)
            return parse_h2load_output(res.stdout)

        elif protocol == "ws":
            wl = WS_WORKLOADS[workload_name]
            url = f"http://127.0.0.1:{port}{wl['path']}"
            cmd = [
                "taskset", "-c", cores,
                GCANNON_BIN,
                "--ws",
                url,
                "-c", str(conns),
                "-t", str(threads),
                "-d", f"{duration}s",
                "-p", str(wl["pipeline"]),
                "--json",
            ]
            if wl.get("req_per_conn", 0) > 0:
                cmd += ["-r", str(wl["req_per_conn"])]
            res = subprocess.run(cmd, capture_output=True, text=True)
            return parse_gcannon_json(res.stdout)

        raise ValueError(f"Unknown protocol: {protocol}")

    def run(self):
        protocols = ["h1_plain", "h1_tls", "h2", "ws"] if "all" in self.args.protocols else self.args.protocols
        servers = ["aegon", "viltrum"] if "all" in self.args.servers else self.args.servers

        print("\n" + "=" * 80)
        print("          HttpArena Unified Benchmark Runner: Aegon vs Viltrum")
        print("=" * 80)
        print(f" Protocols:       {', '.join(protocols)}")
        print(f" Servers:         {', '.join(servers)}")
        print(f" Concurrency:     {self.args.concurrency} connections | {self.args.threads} client threads")
        print(f" Core Allocation: Server -> Cores {self.args.server_cores} | Client -> Cores {self.args.client_cores}")
        print(f" Runs Per Test:   {self.args.runs} (Duration: {self.args.duration}s, Warmup: {self.args.warmup}s)")
        print(f" Tooling:         H1 Plain/TLS: wrk | H2: h2load | WS: gcannon")
        print("=" * 80 + "\n")

        try:
            for proto in protocols:
                workloads_map = WS_WORKLOADS if proto == "ws" else HTTP_WORKLOADS
                if "all" in self.args.workloads:
                    target_workloads = list(workloads_map.keys())
                else:
                    target_workloads = [w for w in self.args.workloads if w in workloads_map]

                if not target_workloads:
                    continue

                print(f"\n>>> Protocol Suite: [{proto.upper()}] <<<")

                for s_key in servers:
                    if self.args.manage_servers:
                        if not self.start_server(s_key):
                            continue

                    s_name = SERVERS_CONFIG[s_key]["name"]

                    for wl_key in target_workloads:
                        wl_desc = workloads_map[wl_key]["desc"]
                        test_key = f"{proto}::{wl_key}"

                        print(f"  -> Testing {s_name:<7} | {test_key:<20} ({wl_desc})...", end="", flush=True)

                        # Warmup
                        if self.args.warmup > 0:
                            try:
                                self.execute_test(s_key, proto, wl_key, self.args.warmup)
                            except Exception:
                                pass
                            time.sleep(0.5)

                        runs_data = []
                        for r in range(self.args.runs):
                            res = self.execute_test(s_key, proto, wl_key, self.args.duration)
                            runs_data.append(res)
                            if r + 1 < self.args.runs:
                                time.sleep(1.0)

                        avg_rps = sum(x["rps"] for x in runs_data) / len(runs_data)
                        avg_lat = sum(x["avg_us"] for x in runs_data) / len(runs_data)
                        p50_lat = sum(x["p50_us"] for x in runs_data) / len(runs_data)
                        p99_lat = sum(x["p99_us"] for x in runs_data) / len(runs_data)
                        mb_s = sum(x["mb_s"] for x in runs_data) / len(runs_data)

                        summary = {
                            "rps": avg_rps,
                            "avg_us": avg_lat,
                            "p50_us": p50_lat,
                            "p99_us": p99_lat,
                            "mb_s": mb_s,
                            "runs": runs_data,
                        }

                        if test_key not in self.results:
                            self.results[test_key] = {}
                        self.results[test_key][s_key] = summary

                        print(f" {avg_rps:10,.1f} req/s | Avg Lat: {avg_lat:7.1f} µs | p99: {p99_lat:7.1f} µs")

                    if self.args.manage_servers:
                        self.stop_server(s_key)

        finally:
            if self.args.manage_servers:
                self.stop_all()

        self.print_summary_table()

        if self.args.json_out:
            with open(self.args.json_out, "w") as f:
                json.dump(self.results, f, indent=2)
            print(f"\n[Artifact] Detailed JSON saved to: {self.args.json_out}")

        if self.args.markdown_out:
            self.save_markdown_summary(self.args.markdown_out)
            print(f"[Artifact] Markdown summary saved to: {self.args.markdown_out}")

    def print_summary_table(self):
        if not self.results:
            return

        print("\n" + "=" * 90)
        print(f" {'BENCHMARK COMPARISON SUMMARY: AEGON VS VILTRUM':^88}")
        print("=" * 90)
        print(f" {'Protocol / Workload':<28} | {'Aegon RPS':<13} | {'Viltrum RPS':<13} | {'Delta RPS':<11} | {'Aegon p99':<10} | {'Viltrum p99':<10}")
        print("-" * 90)

        for test_key, data in self.results.items():
            aegon = data.get("aegon", {})
            viltrum = data.get("viltrum", {})

            a_rps = aegon.get("rps", 0.0)
            v_rps = viltrum.get("rps", 0.0)
            a_p99 = aegon.get("p99_us", 0.0)
            v_p99 = viltrum.get("p99_us", 0.0)

            delta_str = "N/A"
            if v_rps > 0 and a_rps > 0:
                delta = ((a_rps - v_rps) / v_rps) * 100.0
                delta_str = f"{delta:+.1f}%"

            a_rps_str = f"{a_rps:,.0f}" if a_rps > 0 else "N/A"
            v_rps_str = f"{v_rps:,.0f}" if v_rps > 0 else "N/A"
            a_p99_str = f"{a_p99:.1f}µs" if a_p99 > 0 else "N/A"
            v_p99_str = f"{v_p99:.1f}µs" if v_p99 > 0 else "N/A"

            print(f" {test_key:<28} | {a_rps_str:<13} | {v_rps_str:<13} | {delta_str:<11} | {a_p99_str:<10} | {v_p99_str:<10}")

        print("=" * 90 + "\n")

    def save_markdown_summary(self, path: str):
        lines = [
            "# Benchmark Comparison: Aegon vs. Viltrum",
            "",
            f"- **Server Hardware**: Physical Cores {self.args.server_cores} (2 worker threads)",
            f"- **Client Hardware**: Physical Cores {self.args.client_cores} ({self.args.threads} threads, {self.args.concurrency} conns)",
            f"- **Duration**: {self.args.duration}s per test | Runs: {self.args.runs}",
            "",
            "| Protocol / Workload | Aegon (RPS) | Viltrum (RPS) | Δ Throughput | Aegon p99 Latency | Viltrum p99 Latency |",
            "| :--- | :---: | :---: | :---: | :---: | :---: |",
        ]
        for test_key, data in self.results.items():
            aegon = data.get("aegon", {})
            viltrum = data.get("viltrum", {})

            a_rps = aegon.get("rps", 0.0)
            v_rps = viltrum.get("rps", 0.0)
            a_p99 = aegon.get("p99_us", 0.0)
            v_p99 = viltrum.get("p99_us", 0.0)

            delta_str = "N/A"
            if v_rps > 0 and a_rps > 0:
                delta = ((a_rps - v_rps) / v_rps) * 100.0
                delta_str = f"**{delta:+.1f}%**"

            a_rps_str = f"{a_rps:,.0f}" if a_rps > 0 else "N/A"
            v_rps_str = f"{v_rps:,.0f}" if v_rps > 0 else "N/A"
            a_p99_str = f"{a_p99:.1f} µs" if a_p99 > 0 else "N/A"
            v_p99_str = f"{v_p99:.1f} µs" if v_p99 > 0 else "N/A"

            lines.append(f"| `{test_key}` | {a_rps_str} | {v_rps_str} | {delta_str} | {a_p99_str} | {v_p99_str} |")

        lines.append("")
        with open(path, "w") as f:
            f.write("\n".join(lines))

def main():
    parser = argparse.ArgumentParser(description="HttpArena Benchmark Suite: Aegon vs. Viltrum")
    parser.add_argument("-p", "--protocols", nargs="+", default=["all"],
                        choices=["all", "h1_plain", "h1_tls", "h2", "ws"],
                        help="Protocols to run (default: all)")
    parser.add_argument("-w", "--workloads", nargs="+", default=["all"],
                        help="Workloads to run (all, baseline, pipeline, json, delay, echo, limited-conn)")
    parser.add_argument("-s", "--servers", nargs="+", default=["all"],
                        choices=["all", "aegon", "viltrum"],
                        help="Servers to test (default: all)")
    parser.add_argument("-r", "--runs", type=int, default=1,
                        help="Number of iterations per test (default: 1, pass 3 for triplicate)")
    parser.add_argument("-d", "--duration", type=int, default=10,
                        help="Duration of each test in seconds (default: 10)")
    parser.add_argument("--warmup", type=int, default=2,
                        help="Warmup duration in seconds before measurement (default: 2)")
    parser.add_argument("-c", "--concurrency", type=int, default=100,
                        help="Client concurrency / connections (default: 100)")
    parser.add_argument("-t", "--threads", type=int, default=4,
                        help="Client worker threads (default: 4)")
    parser.add_argument("--server-cores", default="0,1",
                        help="CPU affinity for servers (default: '0,1')")
    parser.add_argument("--client-cores", default="2,3,4,5",
                        help="CPU affinity for client load generators (default: '2,3,4,5')")
    parser.add_argument("--no-manage-servers", dest="manage_servers", action="store_false", default=True,
                        help="Do not auto-start/stop servers (assumes servers are already running)")
    parser.add_argument("--json-out", default=None,
                        help="Save results to specified JSON file")
    parser.add_argument("--markdown-out", default=None,
                        help="Save summary table to Markdown file")

    args = parser.parse_args()

    # Expand comma-separated values if passed as -p h1_plain,h2 or -w baseline,json
    expanded_p = []
    for p in args.protocols:
        expanded_p.extend(p.split(","))
    args.protocols = expanded_p

    expanded_w = []
    for w in args.workloads:
        expanded_w.extend(w.split(","))
    args.workloads = expanded_w

    expanded_s = []
    for s in args.servers:
        expanded_s.extend(s.split(","))
    args.servers = expanded_s

    runner = BenchmarkRunner(args)

    def handle_sigint(sig, frame):
        print("\n[Runner] Interrupted, terminating running servers...")
        runner.stop_all()
        sys.exit(1)

    signal.signal(signal.SIGINT, handle_sigint)
    signal.signal(signal.SIGTERM, handle_sigint)

    runner.run()

if __name__ == "__main__":
    main()
