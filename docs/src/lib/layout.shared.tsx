import Image from 'next/image';
import type { BaseLayoutProps } from 'fumadocs-ui/layouts/shared';
import { gitConfig, assetPath } from './shared';

export function baseOptions(): BaseLayoutProps {
  return {
    nav: {
      title: (
        <span className="flex items-center gap-2.5 hover:opacity-90 transition-opacity">
          <Image
            src={assetPath('/logo.svg')}
            alt="Aegon"
            width={78}
            height={24}
            priority
            className="h-6 w-auto object-contain drop-shadow-[0_0_10px_rgba(255,223,102,0.25)]"
          />
          <span className="hidden sm:inline-flex items-center text-[10px] font-mono font-semibold px-1.5 py-0.5 rounded bg-amber-400/10 text-amber-300 border border-amber-400/20">
            C++23
          </span>
        </span>
      ),
    },
    themeSwitch: {
      enabled: false,
    },
    links: [
      {
        text: 'Documentation',
        url: '/docs',
        active: 'nested-url',
      },
      {
        text: 'Benchmarks',
        url: '/#benchmarks',
      },
      {
        text: 'Architecture',
        url: '/#architecture',
      },
      {
        text: 'Quickstart',
        url: '/#quickstart',
      },
    ],
    githubUrl: `https://github.com/${gitConfig.user}/${gitConfig.repo}`,
  };
}
