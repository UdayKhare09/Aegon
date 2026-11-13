'use client';

import { useState } from 'react';
import Link from 'next/link';
import Image from 'next/image';
import { motion } from 'motion/react';
import { Terminal, Copy, Check, ArrowRight, Zap, Shield, Cpu, BookOpen, Layers } from 'lucide-react';
import { GithubIcon } from '@/components/icons/github';
import { assetPath } from '@/lib/shared';

type InstallTab = 'clone' | 'conan' | 'cmake';

const installCommands: Record<InstallTab, { command: string; label: string; language: string }> = {
  clone: {
    command: 'git clone https://github.com/UdayKhare09/Aegon.git',
    label: 'git clone',
    language: 'bash',
  },
  conan: {
    command: 'conan install --requires="aegon/2.4.0" -s compiler.cppstd=23 --build=missing',
    label: 'Conan 2.0',
    language: 'bash',
  },
  cmake: {
    command: 'find_package(aegon CONFIG REQUIRED) # Or via FetchContent',
    label: 'CMake',
    language: 'cmake',
  },
};

export function Hero() {
  const [activeTab, setActiveTab] = useState<InstallTab>('clone');
  const [copied, setCopied] = useState(false);

  const activeCommand = installCommands[activeTab].command;

  const handleCopy = () => {
    navigator.clipboard.writeText(activeCommand);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <section className="relative overflow-hidden pt-12 pb-20 md:pt-20 md:pb-28 aegon-grid-pattern">
      {/* Background radial glow */}
      <div
        aria-hidden="true"
        className="pointer-events-none absolute -top-40 left-1/2 -translate-x-1/2 w-[800px] h-[550px] bg-gradient-to-b from-amber-400/15 via-yellow-500/5 to-transparent blur-3xl -z-10"
      />

      <div className="container mx-auto px-4 max-w-6xl text-center flex flex-col items-center">
        {/* Release / Standard Pill */}
        <motion.div
          initial={{ opacity: 0, y: 15 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.5 }}
          className="inline-flex items-center gap-2 px-3.5 py-1.5 rounded-full border border-amber-400/30 bg-neutral-900/80 text-amber-300 text-xs md:text-sm font-medium mb-8 backdrop-blur-md shadow-sm"
        >
          <span className="relative flex h-2 w-2">
            <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-amber-400 opacity-75" />
            <span className="relative inline-flex rounded-full h-2 w-2 bg-amber-400" />
          </span>
          <span className="font-semibold text-amber-200">HttpArena Verified #1 Benchmark</span>
          <span className="text-neutral-600 hidden sm:inline">&bull;</span>
          <span className="text-neutral-300 hidden sm:inline">C++23 Thread-Per-Core</span>
          <span className="bg-amber-400/20 px-2 py-0.5 rounded text-[11px] font-mono font-bold text-amber-200 border border-amber-400/30">
            v2.4.0
          </span>
        </motion.div>

        {/* Brand Emblem / Spartan Helmet */}
        <motion.div
          initial={{ opacity: 0, scale: 0.9 }}
          animate={{ opacity: 1, scale: 1 }}
          transition={{ duration: 0.6, delay: 0.1 }}
          className="flex justify-center mb-6 relative group"
        >
          <div className="absolute -inset-4 bg-gradient-to-r from-amber-400/20 to-yellow-500/20 rounded-full blur-2xl opacity-60 group-hover:opacity-100 transition-opacity duration-500" />
          <Image
            src={assetPath('/logo.svg')}
            alt="Aegon Spartan Emblem"
            width={120}
            height={120}
            priority
            className="h-20 sm:h-28 md:h-32 w-auto relative drop-shadow-[0_0_35px_rgba(255,223,102,0.35)] transition-transform duration-500 group-hover:scale-105"
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
          <span className="bg-gradient-to-r from-amber-200 via-amber-400 to-yellow-500 bg-clip-text text-transparent drop-shadow-[0_0_20px_rgba(255,223,102,0.2)]">
            Sub-Microsecond
          </span>{' '}
          Latency.
        </motion.h1>

        {/* Subtitle */}
        <motion.p
          initial={{ opacity: 0, y: 20 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.6, delay: 0.3 }}
          className="max-w-3xl text-base sm:text-lg md:text-xl text-neutral-300 mb-10 leading-relaxed font-light"
        >
          A unified, high-performance C++23 application framework engineered around standard coroutines (
          <code className="text-amber-300 font-mono text-sm px-1.5 py-0.5 bg-neutral-900 rounded border border-neutral-700/80">
            co_await
          </code>
          ), kernel-level <span className="text-white font-medium">io_uring</span> / <span className="text-white font-medium">IOCP</span>,
          and reflection-free SQL ORM with multi-tier L2 in-memory caching.
        </motion.p>

        {/* CTA Buttons */}
        <motion.div
          initial={{ opacity: 0, y: 20 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.6, delay: 0.4 }}
          className="flex flex-wrap items-center justify-center gap-3.5 mb-10"
        >
          <Link
            href="/docs"
            className="group relative inline-flex items-center gap-2.5 px-7 py-3.5 rounded-xl font-semibold text-neutral-950 bg-gradient-to-r from-amber-300 via-yellow-400 to-amber-500 hover:brightness-110 shadow-lg shadow-amber-500/25 transition-all duration-200 hover:-translate-y-0.5 active:scale-[0.98]"
          >
            <BookOpen className="w-4 h-4 text-neutral-950" />
            <span>Get Started</span>
            <ArrowRight className="w-4 h-4 transition-transform group-hover:translate-x-1" />
          </Link>

          <a
            href="https://github.com/UdayKhare09/Aegon"
            target="_blank"
            rel="noopener noreferrer"
            className="inline-flex items-center gap-2.5 px-6 py-3.5 rounded-xl font-semibold text-white bg-neutral-900/90 hover:bg-neutral-850 border border-neutral-700/80 hover:border-amber-400/50 backdrop-blur-md transition-all duration-200 hover:-translate-y-0.5 active:scale-[0.98]"
          >
            <GithubIcon className="w-4 h-4" />
            <span>GitHub</span>
            <span className="text-xs px-2 py-0.5 rounded-full bg-neutral-800 text-amber-300/90 font-mono border border-neutral-700">
              ★ Star
            </span>
          </a>

          <a
            href="#architecture"
            className="inline-flex items-center gap-2 px-5 py-3.5 rounded-xl text-sm font-medium text-neutral-300 hover:text-amber-300 hover:bg-neutral-900/60 border border-transparent hover:border-neutral-800 transition-all duration-200"
          >
            <Layers className="w-4 h-4 text-amber-400" />
            <span>Architecture</span>
          </a>
        </motion.div>

        {/* Interactive Quickstart Terminal Box */}
        <motion.div
          initial={{ opacity: 0, y: 20 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.6, delay: 0.5 }}
          className="w-full max-w-xl"
        >
          <div className="rounded-xl border border-neutral-800/90 bg-neutral-950/90 backdrop-blur-xl shadow-2xl overflow-hidden text-left">
            {/* Header / Tabs */}
            <div className="flex items-center justify-between px-3.5 py-2 border-b border-neutral-800/80 bg-neutral-900/70">
              <div className="flex items-center gap-2">
                <div className="flex items-center gap-1.5 mr-2">
                  <span className="w-2.5 h-2.5 rounded-full bg-rose-500/80" />
                  <span className="w-2.5 h-2.5 rounded-full bg-amber-500/80" />
                  <span className="w-2.5 h-2.5 rounded-full bg-emerald-500/80" />
                </div>
                {(['clone', 'conan', 'cmake'] as InstallTab[]).map((tab) => (
                  <button
                    key={tab}
                    onClick={() => setActiveTab(tab)}
                    className={`px-2.5 py-1 rounded text-xs font-mono font-medium transition-colors ${
                      activeTab === tab
                        ? 'bg-neutral-800 text-amber-300 border border-neutral-700'
                        : 'text-neutral-400 hover:text-neutral-200 hover:bg-neutral-850'
                    }`}
                  >
                    {installCommands[tab].label}
                  </button>
                ))}
              </div>

              <button
                onClick={handleCopy}
                className="inline-flex items-center gap-1.5 px-2 py-1 rounded-md text-xs font-mono text-neutral-400 hover:text-white hover:bg-neutral-800 transition-colors border border-transparent hover:border-neutral-700"
                title="Copy to clipboard"
              >
                {copied ? (
                  <>
                    <Check className="w-3.5 h-3.5 text-emerald-400" />
                    <span className="text-emerald-400">Copied</span>
                  </>
                ) : (
                  <>
                    <Copy className="w-3.5 h-3.5" />
                    <span>Copy</span>
                  </>
                )}
              </button>
            </div>

            {/* Command Line Area */}
            <div className="px-4 py-3 flex items-center gap-2 font-mono text-xs sm:text-sm overflow-x-auto aegon-terminal-scrollbar">
              <Terminal className="w-4 h-4 text-amber-400 shrink-0" />
              <span className="text-amber-400 select-none">$</span>
              <span className="text-neutral-200 select-all whitespace-nowrap">
                {activeCommand}
              </span>
            </div>
          </div>
        </motion.div>

        {/* Feature Highlights under Hero */}
        <motion.div
          initial={{ opacity: 0 }}
          animate={{ opacity: 1 }}
          transition={{ duration: 0.8, delay: 0.6 }}
          className="mt-12 grid grid-cols-2 md:grid-cols-4 gap-4 text-neutral-400 text-xs sm:text-sm font-medium w-full max-w-4xl"
        >
          <div className="flex items-center justify-center gap-2 p-2.5 rounded-lg bg-neutral-900/40 border border-neutral-800/60">
            <Zap className="w-4 h-4 text-amber-400 shrink-0" />
            <span className="text-neutral-200">14.4M req/s Peak</span>
          </div>
          <div className="flex items-center justify-center gap-2 p-2.5 rounded-lg bg-neutral-900/40 border border-neutral-800/60">
            <Cpu className="w-4 h-4 text-amber-400 shrink-0" />
            <span className="text-neutral-200">14.9 µs CPU / req</span>
          </div>
          <div className="flex items-center justify-center gap-2 p-2.5 rounded-lg bg-neutral-900/40 border border-neutral-800/60">
            <Shield className="w-4 h-4 text-amber-400 shrink-0" />
            <span className="text-neutral-200">io_uring & IOCP</span>
          </div>
          <div className="flex items-center justify-center gap-2 p-2.5 rounded-lg bg-neutral-900/40 border border-neutral-800/60">
            <Layers className="w-4 h-4 text-amber-400 shrink-0" />
            <span className="text-neutral-200">Lock-Free Cores</span>
          </div>
        </motion.div>
      </div>
    </section>
  );
}
