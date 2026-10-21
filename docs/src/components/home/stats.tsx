'use client';

import { motion } from 'motion/react';
import { Trophy, Gauge, Activity, Cpu, Zap, ArrowUpRight } from 'lucide-react';

const stats = [
  {
    value: '14.4M',
    unit: 'req/sec',
    label: 'HTTP/2 Cleartext Peak',
    description: 'HttpArena #1 of 22 frameworks at 1,024 concurrency',
    rank: '#1 in HttpArena',
    icon: Zap,
  },
  {
    value: '63.6M',
    unit: 'msg/sec',
    label: 'WebSocket Pipeline',
    description: 'HttpArena #1 of 27 frameworks with 1.03ms mean latency',
    rank: '#1 in HttpArena',
    icon: Activity,
  },
  {
    value: '14.9',
    unit: 'µs / req',
    label: 'CPU Cost @ 1M req/s',
    description: 'Measured from cgroup v2 cpu.stat — lowest CPU usage',
    rank: '#1 of 82 Overall',
    icon: Cpu,
  },
  {
    value: '170',
    unit: 'µs',
    label: 'p99 Saturation Latency',
    description: 'Sub-millisecond tail latency at 1M req/s paced load',
    rank: 'Top Tier Paced Load',
    icon: Gauge,
  },
];

export function StatsSection() {
  return (
    <section className="py-16 md:py-24 border-y border-neutral-800/60 bg-neutral-950/40 relative">
      <div className="container mx-auto px-4 max-w-6xl">
        <div className="flex flex-col md:flex-row md:items-end justify-between mb-12">
          <div>
            <div className="inline-flex items-center gap-2 px-3 py-1 rounded-md bg-amber-500/10 border border-amber-500/20 text-amber-300 text-xs font-semibold uppercase tracking-wider mb-3">
              <Trophy className="w-3.5 h-3.5 text-amber-400" />
              <span>HttpArena Independent Verified Benchmarks</span>
            </div>
            <h2 className="text-3xl sm:text-4xl font-bold text-white tracking-tight">
              Scientifically Proven Performance
            </h2>
          </div>
          <a
            href="https://http-arena.com/#type=emerging,flagship&tuned=0"
            target="_blank"
            rel="noopener noreferrer"
            className="inline-flex items-center gap-1.5 text-sm text-neutral-400 hover:text-amber-300 mt-4 md:mt-0 transition-colors group"
          >
            <span>Inspect full methodology on HttpArena</span>
            <ArrowUpRight className="w-4 h-4 transition-transform group-hover:translate-x-0.5 group-hover:-translate-y-0.5" />
          </a>
        </div>

        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-6">
          {stats.map((stat, idx) => {
            const Icon = stat.icon;
            return (
              <motion.div
                key={stat.label}
                initial={{ opacity: 0, y: 20 }}
                whileInView={{ opacity: 1, y: 0 }}
                viewport={{ once: true }}
                transition={{ duration: 0.5, delay: idx * 0.1 }}
                className="aegon-card rounded-2xl p-6 flex flex-col justify-between"
              >
                <div>
                  <div className="flex items-center justify-between mb-4">
                    <div className="p-2 rounded-xl bg-amber-400/10 text-amber-400 border border-amber-400/20">
                      <Icon className="w-5 h-5" />
                    </div>
                    <span className="text-[11px] font-bold px-2 py-0.5 rounded-full bg-amber-400/15 text-amber-300 border border-amber-400/30">
                      {stat.rank}
                    </span>
                  </div>
                  <div className="flex items-baseline gap-1.5 mb-1">
                    <span className="text-4xl font-extrabold text-white tracking-tight">
                      {stat.value}
                    </span>
                    <span className="text-sm font-semibold text-amber-300">
                      {stat.unit}
                    </span>
                  </div>
                  <div className="text-sm font-semibold text-neutral-200 mb-2">
                    {stat.label}
                  </div>
                </div>
                <div className="text-xs text-neutral-400 leading-relaxed border-t border-neutral-800/80 pt-3 mt-2">
                  {stat.description}
                </div>
              </motion.div>
            );
          })}
        </div>
      </div>
    </section>
  );
}
