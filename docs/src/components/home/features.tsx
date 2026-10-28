'use client';

import { motion } from 'motion/react';
import { Cpu, Zap, Network, Database, ShieldCheck, Layers } from 'lucide-react';

const features = [
  {
    icon: Cpu,
    category: 'Concurrency Paradigm',
    title: 'Thread-Per-Core & Shared-Nothing',
    description:
      'Shared-nothing design pinning an isolated event loop to each CPU core. Thread affinity eliminates OS context switches and cache-line invalidation.',
    spec: 'NUMA Topology Pinning',
    status: 'Active',
  },
  {
    icon: Zap,
    category: 'Standard Compliance',
    title: 'Standard C++23 Native Coroutines',
    description:
      'Engineered natively around co_await, co_return, and core::Task<T>. Eliminate callback hell while maintaining zero-overhead stackless dispatch.',
    spec: 'ISO Compliance',
    status: 'GCC 14 / Clang 18',
  },
  {
    icon: ShieldCheck,
    category: 'Kernel Subsystem',
    title: 'io_uring & IOCP Kernel Async',
    description:
      'True kernel-level asynchronous I/O via Linux io_uring and Windows IOCP ring buffers with automatic fallback to high-speed epoll.',
    spec: 'Ring Buffers',
    status: 'Direct SQ/CQ',
  },
  {
    icon: Network,
    category: 'Wire Protocols',
    title: 'Unified Multi-Protocol Engine',
    description:
      'First-class HTTP/1.1 pipelining, HTTP/2 multiplexing, HTTP/3 (QUIC), RFC 6455 WebSockets, and binary gRPC served seamlessly under one router.',
    spec: 'Multiplexing',
    status: 'H1 / H2 / H3 / WS',
  },
  {
    icon: Database,
    category: 'Data & Persistence',
    title: 'Reflection-Free Static ORM & Cache',
    description:
      'Compile-time type-safe database queries with integrated multi-tier L2 in-memory and Redis caching for microsecond data retrieval.',
    spec: 'Wire Serialization',
    status: 'Sub-ms Queries',
  },
  {
    icon: Layers,
    category: 'Hardware & SIMD',
    title: 'Ultra-Low CPU Footprint',
    description:
      'Consumes only 14.9 µs of CPU per request at 1M req/s saturation. SIMD-accelerated AVX-512 and ARM Neon header validation and JSON parsing.',
    spec: 'Vector Acceleration',
    status: 'AVX-512 / Neon',
  },
];

export function FeaturesGrid() {
  return (
    <section id="architecture" className="py-20 md:py-28 bg-neutral-950/40 border-t border-neutral-800/80 relative">
      <div className="container mx-auto px-4 max-w-6xl">
        <div className="text-center max-w-3xl mx-auto mb-16">
          <div className="inline-flex items-center gap-2 px-3 py-1 rounded-md bg-amber-500/10 border border-amber-500/20 text-amber-300 text-xs font-semibold uppercase tracking-wider mb-3">
            <Layers className="w-3.5 h-3.5 text-amber-400" />
            <span>Architecture & Capabilities</span>
          </div>
          <h2 className="text-3xl sm:text-4xl md:text-5xl font-extrabold text-white tracking-tight mb-4">
            Engineered For Extreme Demands
          </h2>
          <p className="text-neutral-400 text-base sm:text-lg font-light">
            Every layer in Aegon is designed for maximum throughput, hardware cache locality, and predictable sub-millisecond tail latency under crushing concurrency.
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
                  <div className="flex items-center justify-between mb-4">
                    <div className="p-3 rounded-xl bg-amber-400/10 text-amber-400 border border-amber-400/20 group-hover:scale-105 group-hover:bg-amber-400/20 transition-all duration-300">
                      <Icon className="w-6 h-6" />
                    </div>
                    <span className="text-[11px] font-mono font-medium px-2.5 py-1 rounded-full bg-neutral-900 text-amber-300/90 border border-neutral-800">
                      {feature.category}
                    </span>
                  </div>
                  <h3 className="text-lg sm:text-xl font-bold text-white mb-2.5 group-hover:text-amber-300 transition-colors">
                    {feature.title}
                  </h3>
                  <p className="text-neutral-400 text-sm leading-relaxed font-light">
                    {feature.description}
                  </p>
                </div>

                <div className="mt-6 pt-4 border-t border-neutral-800/80 flex items-center justify-between text-xs font-mono">
                  <span className="text-neutral-400">{feature.spec}</span>
                  <span className="text-amber-300 font-semibold">{feature.status}</span>
                </div>
              </motion.div>
            );
          })}
        </div>
      </div>
    </section>
  );
}
