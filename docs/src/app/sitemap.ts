import type { MetadataRoute } from 'next';
import { source } from '@/lib/source';

export const revalidate = false;

export default function sitemap(): MetadataRoute.Sitemap {
  const siteUrl = process.env.NEXT_PUBLIC_SITE_URL || 'https://udaykhare09.github.io/Aegon';
  const url = (path: string) => {
    const cleanPath = path.startsWith('/') ? path : `/${path}`;
    return `${siteUrl}${cleanPath}`;
  };

  const pages = source.getPages();

  const docEntries: MetadataRoute.Sitemap = pages.map((page) => ({
    url: url(page.url),
    lastModified: new Date(),
    changeFrequency: 'weekly' as const,
    priority: page.url === '/docs' ? 0.9 : 0.8,
  }));

  return [
    {
      url: url('/'),
      lastModified: new Date(),
      changeFrequency: 'weekly' as const,
      priority: 1.0,
    },
    ...docEntries,
  ];
}

