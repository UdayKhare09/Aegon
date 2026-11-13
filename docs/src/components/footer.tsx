import Link from 'next/link';
import Image from 'next/image';
import { GithubIcon } from '@/components/icons/github';
import { assetPath } from '@/lib/shared';
import { Trophy, Globe, Code2, Heart, ArrowUpRight, ShieldCheck } from 'lucide-react';

export function Footer() {
  return (
    <footer className="w-full border-t border-neutral-800/80 bg-neutral-950/90 text-neutral-400 text-sm mt-auto relative z-10">
      {/* Top highlight glow */}
      <div
        aria-hidden="true"
        className="pointer-events-none absolute top-0 left-1/2 -translate-x-1/2 w-[600px] h-px bg-gradient-to-r from-transparent via-amber-400/40 to-transparent"
      />

      <div className="container mx-auto px-4 py-12 md:py-16 max-w-6xl">
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-8 lg:gap-12 mb-12">
          {/* Brand & Framework Details */}
          <div className="flex flex-col space-y-4">
            <Link href="/" className="inline-flex items-center gap-2.5">
              <Image
                src={assetPath('/logo.svg')}
                alt="Aegon Framework"
                width={78}
                height={24}
                className="h-6 w-auto object-contain drop-shadow-[0_0_10px_rgba(255,223,102,0.25)]"
              />
              <span className="text-[10px] font-mono font-semibold px-1.5 py-0.5 rounded bg-amber-400/10 text-amber-300 border border-amber-400/20">
                C++23
              </span>
            </Link>
            <p className="text-xs text-neutral-400 leading-relaxed">
              Modern thread-per-core asynchronous application framework engineered for extreme HTTP throughput,
              zero-overhead coroutines, and kernel-level io_uring async I/O.
            </p>
            <div className="flex items-center gap-2 text-xs text-neutral-500 font-mono">
              <ShieldCheck className="w-3.5 h-3.5 text-amber-400/80" />
              <span>Released under the MIT License</span>
            </div>
          </div>

          {/* HttpArena Verified Benchmarks */}
          <div className="flex flex-col space-y-3">
            <div className="flex items-center gap-2 text-xs font-semibold uppercase tracking-wider text-amber-300 font-mono">
              <Trophy className="w-3.5 h-3.5 text-amber-400" />
              <span>Verified Benchmarks</span>
            </div>
            <p className="text-xs text-neutral-400">
              Audited by the independent bare-metal benchmarking platform HttpArena:
            </p>
            <ul className="space-y-2 text-xs">
              <li>
                <a
                  href="https://http-arena.com/frameworks/lang/cpp/"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="group inline-flex items-center gap-1.5 text-neutral-300 hover:text-amber-300 transition-colors"
                >
                  <span className="px-1.5 py-0.5 rounded bg-amber-400/15 text-amber-300 font-mono font-bold text-[10px] border border-amber-400/30">
                    #1 in C++
                  </span>
                  <span className="font-medium">HttpArena C++ Benchmark</span>
                  <ArrowUpRight className="w-3 h-3 text-neutral-500 group-hover:text-amber-300 transition-transform group-hover:translate-x-0.5" />
                </a>
              </li>
              <li>
                <a
                  href="https://http-arena.com/frameworks/lang/java/"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="group inline-flex items-center gap-1.5 text-neutral-300 hover:text-amber-300 transition-colors"
                >
                  <span className="px-1.5 py-0.5 rounded bg-amber-400/15 text-amber-300 font-mono font-bold text-[10px] border border-amber-400/30">
                    #1 in Java
                  </span>
                  <span className="font-medium">HttpArena Java FFM Benchmark</span>
                  <ArrowUpRight className="w-3 h-3 text-neutral-500 group-hover:text-amber-300 transition-transform group-hover:translate-x-0.5" />
                </a>
              </li>
              <li>
                <a
                  href="https://http-arena.com/#type=emerging,flagship&tuned=0"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="text-neutral-500 hover:text-neutral-300 transition-colors text-[11px]"
                >
                  Methodology & 100GbE Reproducibility &rarr;
                </a>
              </li>
            </ul>
          </div>

          {/* Creators & Engineering Team */}
          <div className="flex flex-col space-y-3">
            <div className="flex items-center gap-2 text-xs font-semibold uppercase tracking-wider text-amber-300 font-mono">
              <Code2 className="w-3.5 h-3.5 text-amber-400" />
              <span>Core Developers</span>
            </div>
            <ul className="space-y-2.5 text-xs">
              <li>
                <a
                  href="https://github.com/UdayKhare09"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="group flex items-center justify-between p-2 rounded-lg bg-neutral-900/60 hover:bg-neutral-900 border border-neutral-800 hover:border-amber-400/30 transition-all"
                >
                  <div className="flex items-center gap-2">
                    <GithubIcon className="w-3.5 h-3.5 text-neutral-400 group-hover:text-amber-300" />
                    <span className="font-medium text-neutral-200 group-hover:text-amber-200">
                      Uday Khare
                    </span>
                  </div>
                  <span className="text-[10px] font-mono text-neutral-500 group-hover:text-neutral-400">
                    @UdayKhare09
                  </span>
                </a>
              </li>
              <li>
                <a
                  href="https://github.com/Surendra1341"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="group flex items-center justify-between p-2 rounded-lg bg-neutral-900/60 hover:bg-neutral-900 border border-neutral-800 hover:border-amber-400/30 transition-all"
                >
                  <div className="flex items-center gap-2">
                    <GithubIcon className="w-3.5 h-3.5 text-neutral-400 group-hover:text-amber-300" />
                    <span className="font-medium text-neutral-200 group-hover:text-amber-200">
                      Surendra Singh Chouhan
                    </span>
                  </div>
                  <span className="text-[10px] font-mono text-neutral-500 group-hover:text-neutral-400">
                    @Surendra1341
                  </span>
                </a>
              </li>
              <li className="pt-1">
                <a
                  href="https://teamzemo.tech/"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="group inline-flex items-center gap-2 text-neutral-300 hover:text-amber-300 transition-colors"
                >
                  <Globe className="w-3.5 h-3.5 text-amber-400" />
                  <span className="font-medium">Team Zemo Portfolio</span>
                  <ArrowUpRight className="w-3 h-3 text-neutral-500 group-hover:text-amber-300 transition-transform group-hover:translate-x-0.5" />
                </a>
              </li>
            </ul>
          </div>

          {/* Documentation & Resources */}
          <div className="flex flex-col space-y-3">
            <div className="text-xs font-semibold uppercase tracking-wider text-amber-300 font-mono">
              Resources
            </div>
            <ul className="space-y-2 text-xs">
              <li>
                <Link href="/docs" className="hover:text-amber-300 transition-colors">
                  Documentation Hub
                </Link>
              </li>
              <li>
                <Link href="/docs/installation" className="hover:text-amber-300 transition-colors">
                  Installation & Packages
                </Link>
              </li>
              <li>
                <Link href="/docs/usage" className="hover:text-amber-300 transition-colors">
                  Quickstart & REST API
                </Link>
              </li>
              <li>
                <Link href="/docs/http/server" className="hover:text-amber-300 transition-colors">
                  HTTP Server & Routing
                </Link>
              </li>
              <li>
                <a
                  href="https://github.com/UdayKhare09/Aegon"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="inline-flex items-center gap-1 hover:text-amber-300 transition-colors"
                >
                  <span>GitHub Repository</span>
                  <ArrowUpRight className="w-3 h-3 text-neutral-500" />
                </a>
              </li>
            </ul>
          </div>
        </div>

        {/* Bottom Credits & Legal */}
        <div className="pt-8 border-t border-neutral-900 flex flex-col sm:flex-row items-center justify-between gap-4 text-xs text-neutral-500">
          <div className="flex items-center gap-1.5">
            <span>Crafted with</span>
            <Heart className="w-3 h-3 text-amber-400 fill-amber-400" />
            <span>by</span>
            <a
              href="https://teamzemo.tech/"
              target="_blank"
              rel="noopener noreferrer"
              className="text-neutral-400 hover:text-amber-300 font-medium transition-colors"
            >
              Team Zemo
            </a>
            <span>(Uday Khare & Surendra Singh Chouhan)</span>
          </div>

          <div className="flex items-center gap-6">
            <a
              href="https://http-arena.com/frameworks/lang/cpp/"
              target="_blank"
              rel="noopener noreferrer"
              className="hover:text-amber-300 transition-colors font-mono"
            >
              #1 in C++
            </a>
            <a
              href="https://http-arena.com/frameworks/lang/java/"
              target="_blank"
              rel="noopener noreferrer"
              className="hover:text-amber-300 transition-colors font-mono"
            >
              #1 in Java
            </a>
            <a
              href="https://github.com/UdayKhare09/Aegon/blob/main/LICENSE"
              target="_blank"
              rel="noopener noreferrer"
              className="hover:text-neutral-400 transition-colors"
            >
              MIT License
            </a>
          </div>
        </div>
      </div>
    </footer>
  );
}
