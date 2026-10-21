import { createGetUrl } from 'fumadocs-core/source';

export const appName = 'Aegon';
export const docsRoute = '/docs';
export const docsImageRoute = '/og/docs';
export const docsContentRoute = '/llms.mdx/docs';

export const basePath = process.env.NEXT_PUBLIC_BASE_PATH || '';

export function assetPath(path: string): string {
  const clean = path.startsWith('/') ? path : `/${path}`;
  return `${basePath}${clean}`;
}

// fill this with your actual GitHub info, for example:
export const gitConfig = {
  user: 'UdayKhare09',
  repo: 'Aegon',
  branch: 'main',
};

const getContentUrl = createGetUrl(docsContentRoute);

export function getPageMarkdownUrl(page: { slugs: string[]; locale?: string }) {
  const segments = [...page.slugs, 'content.md'];

  return { segments, url: getContentUrl(segments, page.locale) };
}

const getImageUrl = createGetUrl(docsImageRoute);

export function getPageImageUrl(page: { slugs: string[]; locale?: string }) {
  const segments = [...page.slugs, 'image.png'];

  return { segments, url: getImageUrl(segments, page.locale) };
}
