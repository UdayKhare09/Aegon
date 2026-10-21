'use client';

import Link from 'next/link';
import { motion } from 'motion/react';
import { ArrowRight, BookOpen, Terminal, Sparkles } from 'lucide-react';
import { GithubIcon } from '@/components/icons/github';

export function CtaSection() {
  return (
    <section className="py-20 md:py-32 relative overflow-hidden">
      {/* Background Glow */}
      <div 
        aria-hidden="true" 
        className="pointer-events-none absolute bottom-0 left-1/2 -translate-x-1/2 w-[800px] h-[400px] bg-gradient-to-t from-amber-500/10 via-yellow-500/5 to-transparent blur-3xl -z-10" 
      />

      <div className="container mx-auto px-4 max-w-5xl">
        <div className="relative rounded-3xl p-8 sm:p-12 md:p-16 border border-amber-400/30 bg-gradient-to-b from-neutral-900/90 via-neutral-950/90 to-neutral-950/95 backdrop-blur-xl shadow-2xl text-center overflow-hidden">
          {/* Subtle Spartan decorative emblem in corner */}
          <div className="absolute top-0 right-0 w-64 h-64 -mr-16 -mt-16 opacity-5 pointer-events-none">
            <img src="/logo.svg" alt="" className="w-full h-full object-contain" />
          </div>

          <motion.div
            initial={{ opacity: 0, y: 20 }}
            whileInView={{ opacity: 1, y: 0 }}
            viewport={{ once: true }}
            transition={{ duration: 0.6 }}
            className="relative z-10"
          >
            <div className="inline-flex items-center gap-2 px-3 py-1 rounded-full bg-amber-400/10 border border-amber-400/30 text-amber-300 text-xs font-semibold mb-6">
              <Sparkles className="w-3.5 h-3.5 text-amber-400" />
              <span>Get Started in Under 5 Minutes</span>
            </div>

            <h2 className="text-3xl sm:text-4xl md:text-5xl font-black text-white tracking-tight mb-6">
              Ready to Build Without Limits?
            </h2>

            <p className="max-w-2xl mx-auto text-base sm:text-lg text-neutral-300 mb-10 leading-relaxed font-light">
              Experience the unmatched combination of raw C++23 speed, modern coroutines, and complete protocol coverage.
            </p>

            <div className="flex flex-wrap items-center justify-center gap-4">
              <Link
                href="/docs"
                className="group inline-flex items-center gap-2.5 px-8 py-4 rounded-xl font-semibold text-neutral-950 bg-gradient-to-r from-amber-300 via-yellow-400 to-amber-500 hover:brightness-110 shadow-xl shadow-amber-500/20 transition-all hover:-translate-y-0.5"
              >
                <BookOpen className="w-5 h-5 text-neutral-950" />
                <span>Read Documentation</span>
                <ArrowRight className="w-4 h-4 transition-transform group-hover:translate-x-1" />
              </Link>

              <a
                href="https://github.com/UdayKhare09/Aegon"
                target="_blank"
                rel="noopener noreferrer"
                className="inline-flex items-center gap-2.5 px-8 py-4 rounded-xl font-semibold text-white bg-neutral-900/90 hover:bg-neutral-800 border border-neutral-700/80 hover:border-amber-400/50 transition-all hover:-translate-y-0.5"
              >
                <GithubIcon className="w-5 h-5" />
                <span>Star on GitHub</span>
              </a>
            </div>
          </motion.div>
        </div>
      </div>
    </section>
  );
}
