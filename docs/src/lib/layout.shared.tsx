import type { BaseLayoutProps } from 'fumadocs-ui/layouts/shared';
import { gitConfig } from './shared';

export function baseOptions(): BaseLayoutProps {
  return {
    nav: {
      title: (
        <img
          src="/logo.svg"
          alt="Aegon"
          className="h-6 w-auto object-contain drop-shadow-[0_0_10px_rgba(255,223,102,0.2)]"
        />
      ),
    },
    themeSwitch: {
      enabled: false,
    },
    links: [
    ],
    githubUrl: `https://github.com/${gitConfig.user}/${gitConfig.repo}`,
  };
}
