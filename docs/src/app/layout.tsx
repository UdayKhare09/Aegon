import type { Metadata } from 'next';
import { Inter } from 'next/font/google';
import { Provider } from '@/components/provider';
import './global.css';

const inter = Inter({
  subsets: ['latin'],
});

const siteUrl = process.env.NEXT_PUBLIC_SITE_URL || 'https://udaykhare09.github.io/Aegon';
const basePath = process.env.NEXT_PUBLIC_BASE_PATH || '';

export const metadata: Metadata = {
  metadataBase: new URL(siteUrl),
  title: {
    template: '%s | AEGON - High-Performance C++23 Framework',
    default: 'AEGON | #1 High-Performance C++23 Web & Data Framework',
  },
  description:
    'Modern, unified thread-per-core C++23 application framework ranked #1 in C++ and #1 in Java on HttpArena. Engineered for 14.4M req/s, sub-microsecond latency, io_uring, C++23 coroutines, and SIMD acceleration.',
  keywords: [
    'Aegon',
    'Aegon C++',
    'C++23 web framework',
    'C++ web framework',
    'io_uring HTTP server',
    'HttpArena #1 C++',
    'HttpArena #1 Java',
    'fastest C++ web framework',
    'fastest Java web framework',
    'Java 22 FFM web engine',
    'Project Panama HTTP server',
    'thread-per-core architecture',
    'C++ coroutines REST API',
    'Uday Khare',
    'Surendra Singh Chouhan',
    'Team Zemo',
    'SIMD JSON serialization',
    'reflection free SQL ORM',
    'io_uring networking',
  ],
  authors: [
    { name: 'Uday Khare', url: 'https://github.com/UdayKhare09' },
    { name: 'Surendra Singh Chouhan', url: 'https://github.com/Surendra1341' },
  ],
  creator: 'Uday Khare & Surendra Singh Chouhan',
  publisher: 'Team Zemo (https://teamzemo.tech/)',
  verification: {
    google: 'xLzEmHAgavnczw7Q0uSUISXh1d1t1EaNadAKjP6YsJQ',
  },
  alternates: {
    canonical: './',
  },
  openGraph: {
    type: 'website',
    locale: 'en_US',
    url: siteUrl,
    siteName: 'AEGON Framework',
    title: 'AEGON | #1 High-Performance C++23 Web & Data Framework',
    description:
      'Ranked #1 in C++ and #1 in Java on HttpArena. Unified thread-per-core framework engineered for 14.4M req/s, sub-microsecond latency, io_uring, and C++23 coroutines.',
    images: [
      {
        url: `${basePath}/a.svg`,
        width: 800,
        height: 800,
        alt: 'AEGON Spartan Emblem',
      },
    ],
  },
  twitter: {
    card: 'summary_large_image',
    title: 'AEGON | #1 High-Performance C++23 Web Framework',
    description:
      'Ranked #1 in C++ & #1 in Java on HttpArena. Built by Uday Khare & Surendra Singh Chouhan (Team Zemo).',
    images: [`${basePath}/a.svg`],
  },
  robots: {
    index: true,
    follow: true,
    googleBot: {
      index: true,
      follow: true,
      'max-video-preview': -1,
      'max-image-preview': 'large',
      'max-snippet': -1,
    },
  },
  icons: {
    icon: [
      { url: `${basePath}/a.svg`, type: 'image/svg+xml' },
    ],
  },
};

const jsonLd = {
  '@context': 'https://schema.org',
  '@graph': [
    {
      '@type': 'SoftwareApplication',
      '@id': `${siteUrl}/#software`,
      name: 'Aegon',
      operatingSystem: 'Linux, Windows',
      applicationCategory: 'DeveloperApplication',
      description:
        'Unified high-performance C++23 asynchronous web and networking framework ranked #1 in C++ and #1 in Java on HttpArena.',
      license: 'https://opensource.org/licenses/MIT',
      url: siteUrl,
      codeRepository: 'https://github.com/UdayKhare09/Aegon',
      programmingLanguage: ['C++', 'C++23', 'C', 'Java'],
      award: [
        'HttpArena #1 C++ Web Framework (https://http-arena.com/frameworks/lang/cpp/)',
        'HttpArena #1 Java Web Framework (https://http-arena.com/frameworks/lang/java/)',
      ],
      author: [
        {
          '@type': 'Person',
          name: 'Uday Khare',
          url: 'https://github.com/UdayKhare09',
        },
        {
          '@type': 'Person',
          name: 'Surendra Singh Chouhan',
          url: 'https://github.com/Surendra1341',
        },
      ],
      creator: {
        '@type': 'Organization',
        name: 'Team Zemo',
        url: 'https://teamzemo.tech/',
      },
    },
    {
      '@type': 'Organization',
      '@id': 'https://teamzemo.tech/#organization',
      name: 'Team Zemo',
      url: 'https://teamzemo.tech/',
      member: [
        {
          '@type': 'Person',
          name: 'Uday Khare',
          url: 'https://github.com/UdayKhare09',
        },
        {
          '@type': 'Person',
          name: 'Surendra Singh Chouhan',
          url: 'https://github.com/Surendra1341',
        },
      ],
    },
    {
      '@type': 'WebSite',
      '@id': `${siteUrl}/#website`,
      url: siteUrl,
      name: 'Aegon Documentation',
      description: 'Official documentation and API reference for the Aegon C++23 framework.',
      publisher: {
        '@type': 'Organization',
        name: 'Team Zemo',
        url: 'https://teamzemo.tech/',
      },
    },
  ],
};

export default function Layout({ children }: LayoutProps<'/'>) {
  return (
    <html lang="en" className={`${inter.className} dark`} style={{ colorScheme: 'dark' }} suppressHydrationWarning>
      <head>
        <script
          type="application/ld+json"
          dangerouslySetInnerHTML={{ __html: JSON.stringify(jsonLd) }}
        />
      </head>
      <body className="flex flex-col min-h-screen bg-neutral-950 text-neutral-100">
        <Provider>{children}</Provider>
      </body>
    </html>
  );
}
