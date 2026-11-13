'use client';

import Link from 'next/link';
import { motion } from 'motion/react';
import { Rocket, Server, Database, Radio, Shield, HardDrive, ArrowRight, BookOpen, Clock } from 'lucide-react';

const docGuides = [
  {
    icon: Rocket,
    title: 'Getting Started Guide',
    description: 'Install Aegon via Conan or CMake FetchContent and launch your first coroutine HTTP server.',
    href: '/docs',
    readTime: '5 min read',
    badge: 'Quickstart',
  },
  {
    icon: Server,
    title: 'HTTP Server & Routing',
    description: 'Configure high-performance route groups, JSON payload binding, and asynchronous response handlers.',
    href: '/docs/http/server',
    readTime: '7 min read',
    badge: 'Core HTTP',
  },
  {
    icon: Database,
    title: 'Reflection-Free SQL ORM',
    description: 'Declare compile-time models, type-safe queries, transaction handling, and integrated L2 caching.',
    href: '/docs/data/sql/querying',
    readTime: '8 min read',
    badge: 'Data Layer',
  },
  {
    icon: Radio,
    title: 'High-Throughput WebSockets',
    description: 'Build 60M+ msg/s bidirectional streaming services, market data feeds, and core broadcast rings.',
    href: '/docs/http/websocket',
    readTime: '6 min read',
    badge: 'Streaming',
  },
  {
    icon: HardDrive,
    title: 'Multi-Tier Redis & Cache',
    description: 'Configure async Redis cluster clients, topology failover, and thread-local memstore caching.',
    href: '/docs/data/redis/client',
    readTime: '6 min read',
    badge: 'Caching',
  },
  {
    icon: Shield,
    title: 'Middleware & Security',
    description: 'Plug in JWT token verification, CORS headers, rate limiting, and zero-allocation access logging.',
    href: '/docs/http/middleware',
    readTime: '5 min read',
    badge: 'Security',
  },
];

export function DocsHub() {
  return (
    <section id="quickstart" className="py-20 md:py-28 border-t border-neutral-800/80 bg-neutral-950/60 relative">
      <div className="container mx-auto px-4 max-w-6xl">
        <div className="flex flex-col md:flex-row md:items-end justify-between mb-14 gap-6">
          <div>
            <div className="inline-flex items-center gap-2 px-3 py-1 rounded-md bg-amber-500/10 border border-amber-500/20 text-amber-300 text-xs font-semibold uppercase tracking-wider mb-3">
              <BookOpen className="w-3.5 h-3.5 text-amber-400" />
              <span>Developer Knowledge Base</span>
            </div>
            <h2 className="text-3xl sm:text-4xl font-extrabold text-white tracking-tight">
              Documentation & Guides
            </h2>
            <p className="text-neutral-400 text-base mt-2 max-w-xl font-light">
              Explore hands-on guides, API references, and architectural patterns designed for production deployments.
            </p>
          </div>

          <Link
            href="/docs"
            className="inline-flex items-center gap-2 text-sm font-semibold text-amber-400 hover:text-amber-300 transition-colors group shrink-0"
          >
            <span>View all documentation</span>
            <ArrowRight className="w-4 h-4 transition-transform group-hover:translate-x-1" />
          </Link>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
          {docGuides.map((guide, idx) => {
            const Icon = guide.icon;
            return (
              <motion.div
                key={guide.title}
                initial={{ opacity: 0, y: 20 }}
                whileInView={{ opacity: 1, y: 0 }}
                viewport={{ once: true }}
                transition={{ duration: 0.5, delay: idx * 0.07 }}
              >
                <Link
                  href={guide.href}
                  className="aegon-card rounded-2xl p-6 flex flex-col justify-between h-full group block"
                >
                  <div>
                    <div className="flex items-center justify-between mb-4">
                      <div className="p-2.5 rounded-xl bg-amber-400/10 text-amber-400 border border-amber-400/20 group-hover:scale-105 group-hover:bg-amber-400/20 transition-all">
                        <Icon className="w-5 h-5" />
                      </div>
                      <span className="text-[11px] font-mono px-2 py-0.5 rounded-full bg-neutral-900 text-neutral-300 border border-neutral-800">
                        {guide.badge}
                      </span>
                    </div>

                    <h3 className="text-lg font-bold text-white mb-2 group-hover:text-amber-300 transition-colors">
                      {guide.title}
                    </h3>
                    <p className="text-neutral-400 text-sm leading-relaxed font-light">
                      {guide.description}
                    </p>
                  </div>

                  <div className="mt-6 pt-4 border-t border-neutral-800/80 flex items-center justify-between text-xs text-neutral-400 font-mono">
                    <span className="flex items-center gap-1.5 text-neutral-400">
                      <Clock className="w-3.5 h-3.5" />
                      {guide.readTime}
                    </span>
                    <span className="inline-flex items-center gap-1 text-amber-400 group-hover:text-amber-300 font-semibold transition-colors">
                      <span>Explore</span>
                      <ArrowRight className="w-3.5 h-3.5 transition-transform group-hover:translate-x-1" />
                    </span>
                  </div>
                </Link>
              </motion.div>
            );
          })}
        </div>
      </div>
    </section>
  );
}

