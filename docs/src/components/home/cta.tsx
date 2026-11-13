'use client';

import Link from 'next/link';
import Image from 'next/image';
import { motion } from 'motion/react';
import { ArrowRight, BookOpen, Sparkles, Layers, ShieldCheck } from 'lucide-react';
import { GithubIcon } from '@/components/icons/github';
import { assetPath } from '@/lib/shared';

export function CtaSection() {
  return (
    <section className="py-20 md:py-32 relative overflow-hidden">
      {/* Background Glow */}
      <div
        aria-hidden="true"
        className="pointer-events-none absolute bottom-0 left-1/2 -translate-x-1/2 w-[900px] h-[450px] bg-gradient-to-t from-amber-500/15 via-yellow-500/5 to-transparent blur-3xl -z-10"
      />

      <div className="container mx-auto px-4 max-w-5xl">
        <div className="relative rounded-3xl p-8 sm:p-12 md:p-16 border border-amber-400/30 bg-gradient-to-b from-neutral-900/95 via-neutral-950/95 to-neutral-950/98 backdrop-blur-2xl shadow-2xl text-center overflow-hidden aegon-glow">
          {/* Subtle Spartan decorative emblem in corner */}
          <div className="absolute top-0 right-0 w-72 h-72 -mr-16 -mt-16 opacity-10 pointer-events-none select-none">
            <Image
              src={assetPath('/logo.svg')}
              alt=""
              width={288}
              height={288}
              className="w-full h-full object-contain"
            />
          </div>

          <motion.div
            initial={{ opacity: 0, y: 20 }}
            whileInView={{ opacity: 1, y: 0 }}
            viewport={{ once: true }}
            transition={{ duration: 0.6 }}
            className="relative z-10"
          >
            <div className="inline-flex items-center gap-2 px-3.5 py-1.5 rounded-full bg-amber-400/10 border border-amber-400/30 text-amber-300 text-xs font-semibold mb-6">
              <Sparkles className="w-3.5 h-3.5 text-amber-400" />
              <span>Production-Grade C++23 Framework</span>
            </div>

            <h2 className="text-3xl sm:text-4xl md:text-5xl font-black text-white tracking-tight mb-6 max-w-3xl mx-auto leading-tight">
              Build Infrastructure That Never Chokes Under Peak Load.
            </h2>

            <p className="max-w-2xl mx-auto text-base sm:text-lg text-neutral-300 mb-10 leading-relaxed font-light">
              Experience the unmatched combination of raw C++23 speed, zero-overhead coroutines, and complete protocol coverage.
            </p>

            <div className="flex flex-wrap items-center justify-center gap-4 mb-8">
              <Link
                href="/docs"
                className="group inline-flex items-center gap-2.5 px-8 py-4 rounded-xl font-semibold text-neutral-950 bg-gradient-to-r from-amber-300 via-yellow-400 to-amber-500 hover:brightness-110 shadow-xl shadow-amber-500/25 transition-all hover:-translate-y-0.5 active:scale-[0.98]"
              >
                <BookOpen className="w-5 h-5 text-neutral-950" />
                <span>Read Documentation</span>
                <ArrowRight className="w-4 h-4 transition-transform group-hover:translate-x-1" />
              </Link>

              <a
                href="https://github.com/UdayKhare09/Aegon"
                target="_blank"
                rel="noopener noreferrer"
                className="inline-flex items-center gap-2.5 px-8 py-4 rounded-xl font-semibold text-white bg-neutral-900/90 hover:bg-neutral-800 border border-neutral-700/80 hover:border-amber-400/50 transition-all hover:-translate-y-0.5 active:scale-[0.98]"
              >
                <GithubIcon className="w-5 h-5" />
                <span>Star on GitHub</span>
              </a>

              <a
                href="#architecture"
                className="inline-flex items-center gap-2 px-6 py-4 rounded-xl font-medium text-neutral-300 hover:text-amber-300 hover:bg-neutral-900/60 border border-transparent hover:border-neutral-800 transition-all duration-200"
              >
                <Layers className="w-4 h-4 text-amber-400" />
                <span>View Architecture</span>
              </a>
            </div>

            {/* Trust / Spec Badges */}
            <div className="pt-8 border-t border-neutral-800/80 flex flex-wrap items-center justify-center gap-6 text-xs font-mono text-neutral-400">
              <span className="flex items-center gap-1.5">
                <ShieldCheck className="w-4 h-4 text-amber-400" />
                <span>100% ISO C++23 Standard</span>
              </span>
              <span className="text-neutral-700 hidden sm:inline">&bull;</span>
              <span>Sub-Microsecond Latency</span>
              <span className="text-neutral-700 hidden sm:inline">&bull;</span>
              <span>Apache-2.0 Open Source</span>
            </div>
          </motion.div>
        </div>
      </div>
    </section>
  );
}
