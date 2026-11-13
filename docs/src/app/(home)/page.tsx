import type { Metadata } from 'next';
import { Hero } from '@/components/home/hero';
import { StatsSection } from '@/components/home/stats';
import { CodeShowcase } from '@/components/home/code-showcase';
import { FeaturesGrid } from '@/components/home/features';
import { DocsHub } from '@/components/home/docs-hub';
import { CtaSection } from '@/components/home/cta';
import { Footer } from '@/components/footer';

const siteUrl = process.env.NEXT_PUBLIC_SITE_URL || 'https://udaykhare09.github.io/Aegon';

export const metadata: Metadata = {
  title: 'AEGON | #1 High-Performance C++23 Web & Data Framework',
  description:
    'A unified thread-per-core C++23 application framework ranked #1 in C++ and #1 in Java on HttpArena. Engineered for 14.4M req/s, sub-microsecond latency, standard coroutines, io_uring, and reflection-free SQL ORM.',
  alternates: {
    canonical: './',
  },
  openGraph: {
    title: 'AEGON | #1 High-Performance C++23 Web & Data Framework',
    description:
      'Ranked #1 in C++ and #1 in Java on HttpArena. Extreme throughput, sub-microsecond latency, standard C++23 coroutines, and io_uring.',
    url: siteUrl,
    siteName: 'AEGON Framework',
    type: 'website',
  },
  twitter: {
    card: 'summary_large_image',
    title: 'AEGON | #1 High-Performance C++23 Web Framework',
    description:
      'Ranked #1 in C++ & #1 in Java on HttpArena. Built by Uday Khare & Surendra Singh Chouhan (Team Zemo).',
  },
};

export default function HomePage() {
  return (
    <div className="flex flex-col flex-1 w-full bg-neutral-950 text-neutral-100 selection:bg-amber-400 selection:text-neutral-950">
      <Hero />
      <StatsSection />
      <CodeShowcase />
      <FeaturesGrid />
      <DocsHub />
      <CtaSection />
      <Footer />
    </div>
  );
}
