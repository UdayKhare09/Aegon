import { Hero } from '@/components/home/hero';
import { StatsSection } from '@/components/home/stats';
import { CodeShowcase } from '@/components/home/code-showcase';
import { FeaturesGrid } from '@/components/home/features';
import { DocsHub } from '@/components/home/docs-hub';
import { CtaSection } from '@/components/home/cta';

export const metadata = {
  title: 'AEGON | High-Performance C++23 Asynchronous Web & Data Framework',
  description:
    'A unified thread-per-core C++23 application framework engineered for extreme throughput, sub-microsecond latency, standard coroutines, and reflection-free SQL ORM.',
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
    </div>
  );
}
