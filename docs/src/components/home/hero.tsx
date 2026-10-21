'use client';

import { useState } from 'react';
import Link from 'next/link';
import { motion } from 'motion/react';
import { Terminal, Copy, Check, ArrowRight, Sparkles, Zap, Shield, Cpu } from 'lucide-react';
import { GithubIcon } from '@/components/icons/github';
import { assetPath } from '@/lib/shared';

export function Hero() {
  const [copied, setCopied] = useState(false);
  const cloneCommand = 'git clone https://github.com/UdayKhare09/Aegon.git';

  const handleCopy = () => {
    navigator.clipboard.writeText(cloneCommand);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <section className="relative overflow-hidden pt-12 pb-20 md:pt-20 md:pb-32 aegon-grid-pattern">
      {/* Background radial glow */}
      <div 
        aria-hidden="true" 
        className="pointer-events-none absolute -top-40 left-1/2 -translate-x-1/2 w-[700px] h-[500px] bg-gradient-to-b from-amber-400/15 via-yellow-500/5 to-transparent blur-3xl -z-10" 
      />

      <div className="container mx-auto px-4 max-w-6xl text-center flex flex-col items-center">
        {/* Release / Standard Pill */}
        <motion.div
          initial={{ opacity: 0, y: 15 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.5 }}
          className="inline-flex items-center gap-2 px-3.5 py-1.5 rounded-full border border-amber-400/30 bg-amber-400/10 text-amber-300 text-xs md:text-sm font-medium mb-8 backdrop-blur-sm"
        >
          <Sparkles className="w-4 h-4 text-amber-400 animate-pulse" />
          <span>C++23 Native &bull; Thread-Per-Core Asynchronous Engine</span>
          <span className="bg-amber-400/20 px-1.5 py-0.5 rounded text-[11px] font-semibold text-amber-200">
            HttpArena #1
          </span>
        </motion.div>

        {/* Brand Emblem / Name Logo */}
        <motion.div
          initial={{ opacity: 0, scale: 0.9 }}
          animate={{ opacity: 1, scale: 1 }}
          transition={{ duration: 0.6, delay: 0.1 }}
          className="flex justify-center mb-6"
        >
          <img
            src={assetPath('/logo.svg')}
            alt="AEGON"
            className="h-20 sm:h-28 md:h-36 w-auto drop-shadow-[0_0_35px_rgba(255,223,102,0.3)] transition-transform duration-500 hover:scale-105"
          />
        </motion.div>

        {/* Main Headline */}
        <motion.h1
          initial={{ opacity: 0, y: 20 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.6, delay: 0.2 }}
          className="text-4xl sm:text-5xl md:text-6xl lg:text-7xl font-extrabold tracking-tight text-white mb-6 leading-[1.1]"
        >
          Extreme Throughput.{' '}
          <span className="bg-gradient-to-r from-amber-200 via-amber-400 to-yellow-500 bg-clip-text text-transparent">
            Sub-Microsecond
          </span>{' '}
          Latency.
        </motion.h1>

        {/* Subtitle */}
        <motion.p
          initial={{ opacity: 0, y: 20 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.6, delay: 0.3 }}
          className="max-w-3xl text-lg sm:text-xl text-neutral-300 mb-10 leading-relaxed font-light"
        >
          A unified, high-performance C++23 web & data framework engineered around standard coroutines (
          <code className="text-amber-300 font-mono text-sm px-1.5 py-0.5 bg-neutral-800/80 rounded border border-neutral-700">
            co_await
          </code>
          ), kernel-level <span className="text-white font-medium">io_uring</span> / <span className="text-white font-medium">IOCP</span>,
          and reflection-free SQL ORM with multi-tier L2 caching.
        </motion.p>

        {/* CTA Buttons */}
        <motion.div
          initial={{ opacity: 0, y: 20 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.6, delay: 0.4 }}
          className="flex flex-wrap items-center justify-center gap-4 mb-12"
        >
          <Link
            href="/docs"
            className="group relative inline-flex items-center gap-2.5 px-7 py-3.5 rounded-xl font-semibold text-neutral-950 bg-gradient-to-r from-amber-300 via-yellow-400 to-amber-500 hover:brightness-110 shadow-lg shadow-amber-500/20 transition-all duration-200 hover:-translate-y-0.5"
          >
            <span>Get Started</span>
            <ArrowRight className="w-4 h-4 transition-transform group-hover:translate-x-1" />
          </Link>

          <a
            href="https://github.com/UdayKhare09/Aegon"
            target="_blank"
            rel="noopener noreferrer"
            className="inline-flex items-center gap-2.5 px-7 py-3.5 rounded-xl font-semibold text-white bg-neutral-900/80 hover:bg-neutral-850 border border-neutral-700/80 hover:border-amber-400/50 backdrop-blur-md transition-all duration-200 hover:-translate-y-0.5"
          >
            <GithubIcon className="w-5 h-5" />
            <span>GitHub</span>
          </a>
        </motion.div>

        {/* Quick Clone Terminal Box */}
        <motion.div
          initial={{ opacity: 0, y: 20 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.6, delay: 0.5 }}
          className="w-full max-w-xl"
        >
          <div className="flex items-center justify-between px-4 py-2.5 bg-neutral-950/80 border border-neutral-800 rounded-xl backdrop-blur-md shadow-2xl">
            <div className="flex items-center gap-2 text-neutral-400 font-mono text-sm overflow-x-auto select-all pr-2">
              <Terminal className="w-4 h-4 text-amber-400 shrink-0" />
              <span className="text-neutral-500">$</span>
              <span className="text-neutral-200 whitespace-nowrap">{cloneCommand}</span>
            </div>
            <button
              onClick={handleCopy}
              className="p-1.5 rounded-lg text-neutral-400 hover:text-white hover:bg-neutral-800 transition-colors shrink-0 ml-2"
              title="Copy to clipboard"
            >
              {copied ? (
                <Check className="w-4 h-4 text-emerald-400" />
              ) : (
                <Copy className="w-4 h-4" />
              )}
            </button>
          </div>
        </motion.div>

        {/* Feature Badges under Hero */}
        <motion.div
          initial={{ opacity: 0 }}
          animate={{ opacity: 1 }}
          transition={{ duration: 0.8, delay: 0.6 }}
          className="mt-12 grid grid-cols-2 sm:grid-cols-3 gap-6 text-neutral-400 text-xs sm:text-sm font-medium"
        >
          <div className="flex items-center justify-center gap-2">
            <Zap className="w-4 h-4 text-amber-400" />
            <span>14.4M req/s Peak Throughput</span>
          </div>
          <div className="flex items-center justify-center gap-2">
            <Cpu className="w-4 h-4 text-amber-400" />
            <span>14.9 µs CPU Cost / Request</span>
          </div>
          <div className="flex items-center justify-center gap-2 col-span-2 sm:col-span-1">
            <Shield className="w-4 h-4 text-amber-400" />
            <span>Zero-Copy io_uring & IOCP</span>
          </div>
        </motion.div>
      </div>
    </section>
  );
}
