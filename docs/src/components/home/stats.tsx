'use client';

import { motion } from 'motion/react';
import { Trophy, Gauge, Activity, Cpu, Zap, ArrowUpRight, CheckCircle2 } from 'lucide-react';

const stats = [
  {
    value: '14.4M',
    unit: 'req/sec',
    label: 'HTTP/2 Cleartext Peak',
    description: 'HttpArena #1 of 22 frameworks at 1,024 concurrency with zero-copy packet path.',
    rank: '#1 in HttpArena',
    delta: '+310% vs Actix · +440% vs Go',
    icon: Zap,
  },
  {
    value: '63.6M',
    unit: 'msg/sec',
    label: 'WebSocket Broadcast',
    description: 'HttpArena #1 of 27 frameworks with 1.03ms mean latency across 96 worker threads.',
    rank: '#1 in HttpArena',
    delta: 'Zero-copy ring buffer fan-out',
    icon: Activity,
  },
  {
    value: '14.9',
    unit: 'µs / req',
    label: 'CPU Cost @ 1M req/s',
    description: 'Measured directly from cgroup v2 cpu.stat — lowest CPU consumption in class.',
    rank: '#1 of 82 Overall',
    delta: 'Sub-core L1/L2 cache pinning',
    icon: Cpu,
  },
  {
    value: '170',
    unit: 'µs',
    label: 'p99 Saturation Latency',
    description: 'Sub-millisecond tail latency maintained under continuous saturated concurrency.',
    rank: 'Top Tier Paced Load',
    delta: 'Deterministic zero jitter',
    icon: Gauge,
  },
];

const latencyBenchmarks = [
  { name: 'Aegon (C++23 io_uring)', latency: '170 µs', pct: 14, isLeader: true },
  { name: 'Seastar (C++20 epoll)', latency: '320 µs', pct: 26, isLeader: false },
  { name: 'Actix-Web (Rust)', latency: '680 µs', pct: 55, isLeader: false },
  { name: 'Go Fiber / Fasthttp', latency: '1,240 µs', pct: 100, isLeader: false },
];

export function StatsSection() {
  return (
    <section id="benchmarks" className="py-20 md:py-28 border-y border-neutral-800/80 bg-neutral-950/60 relative">
      <div className="container mx-auto px-4 max-w-6xl">
        {/* Section Header */}
        <div className="flex flex-col md:flex-row md:items-end justify-between mb-12 gap-6">
          <div>
            <div className="inline-flex items-center gap-2 px-3 py-1 rounded-md bg-amber-500/10 border border-amber-500/20 text-amber-300 text-xs font-semibold uppercase tracking-wider mb-3">
              <Trophy className="w-3.5 h-3.5 text-amber-400" />
              <span>HttpArena Independent Verified Benchmarks</span>
            </div>
            <h2 className="text-3xl sm:text-4xl font-extrabold text-white tracking-tight">
              Scientifically Proven Performance
            </h2>
            <p className="text-neutral-400 text-sm sm:text-base mt-2 max-w-2xl font-light">
              Audited under saturated 100GbE synthetic stress testing and validated via Linux cgroup v2 kernel accounting.
            </p>
          </div>
          <a
            href="https://http-arena.com/#type=emerging,flagship&tuned=0"
            target="_blank"
            rel="noopener noreferrer"
            className="inline-flex items-center gap-1.5 text-sm font-medium text-amber-400 hover:text-amber-300 transition-colors group shrink-0"
          >
            <span>Inspect full methodology</span>
            <ArrowUpRight className="w-4 h-4 transition-transform group-hover:translate-x-0.5 group-hover:-translate-y-0.5" />
          </a>
        </div>

        {/* 4 Telemetry Metric Cards */}
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-6 mb-12">
          {stats.map((stat, idx) => {
            const Icon = stat.icon;
            return (
              <motion.div
                key={stat.label}
                initial={{ opacity: 0, y: 20 }}
                whileInView={{ opacity: 1, y: 0 }}
                viewport={{ once: true }}
                transition={{ duration: 0.5, delay: idx * 0.08 }}
                className="aegon-card rounded-2xl p-6 flex flex-col justify-between"
              >
                <div>
                  <div className="flex items-center justify-between mb-4">
                    <div className="p-2.5 rounded-xl bg-amber-400/10 text-amber-400 border border-amber-400/20">
                      <Icon className="w-5 h-5" />
                    </div>
                    <span className="text-[11px] font-mono font-bold px-2 py-0.5 rounded-full bg-amber-400/15 text-amber-300 border border-amber-400/30">
                      {stat.rank}
                    </span>
                  </div>
                  <div className="flex items-baseline gap-1.5 mb-1">
                    <span className="text-4xl font-extrabold text-white tracking-tight font-mono">
                      {stat.value}
                    </span>
                    <span className="text-sm font-semibold text-amber-300 font-mono">
                      {stat.unit}
                    </span>
                  </div>
                  <div className="text-sm font-semibold text-neutral-200 mb-2">
                    {stat.label}
                  </div>
                  <p className="text-xs text-neutral-400 leading-relaxed">
                    {stat.description}
                  </p>
                </div>
                <div className="mt-4 pt-3 border-t border-neutral-800/80">
                  <div className="inline-flex items-center gap-1.5 text-[11px] font-mono text-amber-300/90 bg-amber-400/5 px-2 py-1 rounded border border-amber-400/15">
                    <CheckCircle2 className="w-3 h-3 text-amber-400 shrink-0" />
                    <span className="truncate">{stat.delta}</span>
                  </div>
                </div>
              </motion.div>
            );
          })}
        </div>

        {/* Latency Profile Telemetry Chart */}
        <motion.div
          initial={{ opacity: 0, y: 20 }}
          whileInView={{ opacity: 1, y: 0 }}
          viewport={{ once: true }}
          transition={{ duration: 0.6 }}
          className="rounded-2xl border border-neutral-800/90 bg-neutral-900/40 backdrop-blur-md p-6 sm:p-8"
        >
          <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 mb-6 border-b border-neutral-800/70 pb-4">
            <div>
              <h3 className="text-lg sm:text-xl font-bold text-white tracking-tight">
                p99 Tail Latency Profile Under Stress
              </h3>
              <p className="text-xs sm:text-sm text-neutral-400 mt-0.5">
                Microseconds at 10M concurrent client saturation (lower is better).
              </p>
            </div>
            <div className="flex items-center gap-4 text-xs font-mono">
              <div className="flex items-center gap-1.5 text-amber-300">
                <span className="w-2.5 h-2.5 rounded-full bg-amber-400" />
                <span>Aegon</span>
              </div>
              <div className="flex items-center gap-1.5 text-neutral-400">
                <span className="w-2.5 h-2.5 rounded-full bg-neutral-600" />
                <span>Other Frameworks</span>
              </div>
            </div>
          </div>

          <div className="space-y-4">
            {latencyBenchmarks.map((bench) => (
              <div key={bench.name}>
                <div className="flex justify-between items-center text-xs sm:text-sm font-mono mb-1.5">
                  <div className="flex items-center gap-2">
                    <span className={bench.isLeader ? 'text-amber-300 font-bold' : 'text-neutral-300'}>
                      {bench.name}
                    </span>
                    {bench.isLeader && (
                      <span className="text-[10px] px-1.5 py-0.2 rounded bg-amber-400/15 text-amber-300 border border-amber-400/30 font-semibold">
                        Lowest Latency
                      </span>
                    )}
                  </div>
                  <span className={bench.isLeader ? 'text-amber-300 font-bold' : 'text-neutral-400'}>
                    {bench.latency}
                  </span>
                </div>
                <div className="w-full h-2.5 bg-neutral-950 rounded-full overflow-hidden border border-neutral-800/80">
                  <div
                    className={`h-full rounded-full transition-all duration-700 ${
                      bench.isLeader
                        ? 'bg-gradient-to-r from-amber-400 to-yellow-400 shadow-sm shadow-amber-500/50'
                        : 'bg-neutral-700/80'
                    }`}
                    style={{ width: `${bench.pct}%` }}
                  />
                </div>
              </div>
            ))}
          </div>
        </motion.div>
      </div>
    </section>
  );
}
