'use client';

import { motion } from 'motion/react';
import { Cpu, Zap, Network, Database, ShieldCheck, Layers, GitBranch, Terminal } from 'lucide-react';

const features = [
  {
    icon: Cpu,
    title: 'Thread-Per-Core Architecture',
    description: 'Shared-nothing design pinning an event loop to each CPU core. Zero lock contention on the critical request path.',
    badge: 'Hardware Optimized',
  },
  {
    icon: Zap,
    title: 'Standard C++23 Coroutines',
    description: 'Engineered entirely around co_await and core::Task<T>. Eliminate callback hell while maintaining zero-overhead stackless dispatch.',
    badge: 'C++23 Native',
  },
  {
    icon: ShieldCheck,
    title: 'io_uring & IOCP Kernel Async',
    description: 'True kernel-level asynchronous I/O via Linux io_uring and Windows IOCP with automatic fallback to high-speed epoll.',
    badge: 'Kernel Zero-Copy',
  },
  {
    icon: Network,
    title: 'Unified Multi-Protocol',
    description: 'First-class HTTP/1.1, HTTP/2 (Cleartext & TLS), HTTP/3 (QUIC), and RFC 6455 WebSockets served seamlessly under one router.',
    badge: 'H1 / H2 / H3 / WS',
  },
  {
    icon: Database,
    title: 'Reflection-Free ORM & L2 Cache',
    description: 'Compile-time type-safe database queries with integrated multi-tier L2 in-memory and Redis caching for microsecond data retrieval.',
    badge: 'Sub-ms Queries',
  },
  {
    icon: Layers,
    title: 'Ultra-Low CPU Footprint',
    description: 'Consumes only 14.9 µs of CPU per request at 1 Million req/s saturation. Verified by HttpArena cgroup v2 metrics.',
    badge: '14.9 µs / req',
  },
];

export function FeaturesGrid() {
  return (
    <section className="py-20 md:py-28 bg-neutral-950/60 border-t border-neutral-850 relative">
      <div className="container mx-auto px-4 max-w-6xl">
        <div className="text-center max-w-3xl mx-auto mb-16">
          <div className="inline-flex items-center gap-2 px-3 py-1 rounded-md bg-amber-500/10 border border-amber-500/20 text-amber-300 text-xs font-semibold uppercase tracking-wider mb-3">
            <Layers className="w-3.5 h-3.5 text-amber-400" />
            <span>Architecture & Capabilities</span>
          </div>
          <h2 className="text-3xl sm:text-4xl md:text-5xl font-extrabold text-white tracking-tight mb-4">
            Engineered For Extreme Demands
          </h2>
          <p className="text-neutral-400 text-base sm:text-lg">
            Every layer in Aegon is designed for maximum throughput, hardware cache locality, and predictable latency under crushing concurrency.
          </p>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
          {features.map((feature, idx) => {
            const Icon = feature.icon;
            return (
              <motion.div
                key={feature.title}
                initial={{ opacity: 0, y: 20 }}
                whileInView={{ opacity: 1, y: 0 }}
                viewport={{ once: true }}
                transition={{ duration: 0.5, delay: idx * 0.08 }}
                className="aegon-card rounded-2xl p-7 flex flex-col justify-between group"
              >
                <div>
                  <div className="flex items-center justify-between mb-5">
                    <div className="p-3 rounded-xl bg-amber-400/10 text-amber-400 border border-amber-400/20 group-hover:scale-110 group-hover:bg-amber-400/20 transition-all duration-300">
                      <Icon className="w-6 h-6" />
                    </div>
                    <span className="text-[11px] font-semibold px-2.5 py-1 rounded-full bg-neutral-900 text-neutral-300 border border-neutral-800">
                      {feature.badge}
                    </span>
                  </div>
                  <h3 className="text-xl font-bold text-white mb-2.5 group-hover:text-amber-300 transition-colors">
                    {feature.title}
                  </h3>
                  <p className="text-neutral-400 text-sm leading-relaxed">
                    {feature.description}
                  </p>
                </div>
              </motion.div>
            );
          })}
        </div>
      </div>
    </section>
  );
}
