import { source } from '@/lib/source';
import {
  DocsBody,
  DocsDescription,
  DocsPage,
  DocsTitle,
  MarkdownCopyButton,
  ViewOptionsPopover,
} from 'fumadocs-ui/layouts/spacious/page';
import { notFound } from 'next/navigation';
import { getMDXComponents } from '@/components/mdx';
import type { Metadata } from 'next';
import { createRelativeLink } from 'fumadocs-ui/mdx';
import { getPageImageUrl, getPageMarkdownUrl, gitConfig } from '@/lib/shared';
import { Trophy, Code2, ArrowUpRight } from 'lucide-react';

const siteUrl = process.env.NEXT_PUBLIC_SITE_URL || 'https://udaykhare09.github.io/Aegon';

export default async function Page(props: PageProps<'/docs/[[...slug]]'>) {
  const params = await props.params;
  const page = source.getPage(params.slug);
  if (!page) notFound();

  const MDX = page.data.body;
  const markdownUrl = getPageMarkdownUrl(page).url;
  const pageFullUrl = `${siteUrl}${page.url}`;

  const articleJsonLd = {
    '@context': 'https://schema.org',
    '@type': 'TechArticle',
    headline: page.data.title,
    description: page.data.description,
    url: pageFullUrl,
    inLanguage: 'en',
    mainEntityOfPage: {
      '@type': 'WebPage',
      '@id': pageFullUrl,
    },
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
    publisher: {
      '@type': 'Organization',
      name: 'Team Zemo',
      url: 'https://teamzemo.tech/',
    },
  };

  const breadcrumbJsonLd = {
    '@context': 'https://schema.org',
    '@type': 'BreadcrumbList',
    itemListElement: [
      {
        '@type': 'ListItem',
        position: 1,
        name: 'Home',
        item: siteUrl,
      },
      {
        '@type': 'ListItem',
        position: 2,
        name: 'Docs',
        item: `${siteUrl}/docs`,
      },
      {
        '@type': 'ListItem',
        position: 3,
        name: page.data.title,
        item: pageFullUrl,
      },
    ],
  };

  return (
    <DocsPage
      toc={page.data.toc}
      tableOfContent={{ enabled: true }}
      full={true}
      className="max-w-none w-full"
    >
      <script
        type="application/ld+json"
        dangerouslySetInnerHTML={{ __html: JSON.stringify(articleJsonLd) }}
      />
      <script
        type="application/ld+json"
        dangerouslySetInnerHTML={{ __html: JSON.stringify(breadcrumbJsonLd) }}
      />

      <DocsTitle>{page.data.title}</DocsTitle>
      <DocsDescription className="mb-0">{page.data.description}</DocsDescription>
      <div className="flex flex-row gap-2 items-center border-b pb-6">
        <MarkdownCopyButton markdownUrl={markdownUrl} />
        <ViewOptionsPopover
          markdownUrl={markdownUrl}
          githubUrl={`https://github.com/${gitConfig.user}/${gitConfig.repo}/blob/${gitConfig.branch}/content/docs/${page.path}`}
        />
      </div>
      <DocsBody className="max-w-none">
        <MDX
          components={getMDXComponents({
            // this allows you to link to other pages with relative file paths
            a: createRelativeLink(source, page),
          })}
        />

        {/* Documentation Footer Attribution */}
        <div className="mt-16 pt-8 border-t border-neutral-800/80 flex flex-col sm:flex-row items-start sm:items-center justify-between gap-4 text-xs text-neutral-400 not-prose">
          <div className="flex flex-wrap items-center gap-2">
            <span className="flex items-center gap-1.5 font-medium text-neutral-300">
              <Code2 className="w-3.5 h-3.5 text-amber-400" />
              <span>Developed by</span>
            </span>
            <a
              href="https://github.com/UdayKhare09"
              target="_blank"
              rel="noopener noreferrer"
              className="text-amber-300 hover:text-amber-200 hover:underline"
            >
              Uday Khare
            </a>
            <span className="text-neutral-600">&</span>
            <a
              href="https://github.com/Surendra1341"
              target="_blank"
              rel="noopener noreferrer"
              className="text-amber-300 hover:text-amber-200 hover:underline"
            >
              Surendra Singh Chouhan
            </a>
            <span className="text-neutral-500">
              (
              <a
                href="https://teamzemo.tech/"
                target="_blank"
                rel="noopener noreferrer"
                className="hover:text-amber-300 underline underline-offset-2"
              >
                Team Zemo
              </a>
              )
            </span>
          </div>

          <div className="flex items-center gap-3 font-mono text-[11px]">
            <a
              href="https://http-arena.com/frameworks/lang/cpp/"
              target="_blank"
              rel="noopener noreferrer"
              className="inline-flex items-center gap-1 text-amber-400 hover:text-amber-300 transition-colors"
            >
              <Trophy className="w-3 h-3" />
              <span>#1 in C++</span>
              <ArrowUpRight className="w-3 h-3 opacity-60" />
            </a>
            <span className="text-neutral-700">•</span>
            <a
              href="https://http-arena.com/frameworks/lang/java/"
              target="_blank"
              rel="noopener noreferrer"
              className="inline-flex items-center gap-1 text-amber-400 hover:text-amber-300 transition-colors"
            >
              <Trophy className="w-3 h-3" />
              <span>#1 in Java</span>
              <ArrowUpRight className="w-3 h-3 opacity-60" />
            </a>
          </div>
        </div>
      </DocsBody>
    </DocsPage>
  );
}

export async function generateStaticParams() {
  return source.generateParams();
}

export async function generateMetadata(props: PageProps<'/docs/[[...slug]]'>): Promise<Metadata> {
  const params = await props.params;
  const page = source.getPage(params.slug);
  if (!page) notFound();

  const title = `${page.data.title} | Aegon C++23 Framework`;
  const description = page.data.description;
  const imageUrl = getPageImageUrl(page).url;
  const pageUrl = `${siteUrl}${page.url}`;

  return {
    title: page.data.title,
    description: page.data.description,
    authors: [
      { name: 'Uday Khare', url: 'https://github.com/UdayKhare09' },
      { name: 'Surendra Singh Chouhan', url: 'https://github.com/Surendra1341' },
    ],
    creator: 'Uday Khare & Surendra Singh Chouhan',
    publisher: 'Team Zemo (https://teamzemo.tech/)',
    alternates: {
      canonical: pageUrl,
    },
    openGraph: {
      title,
      description,
      type: 'article',
      url: pageUrl,
      siteName: 'AEGON Framework Documentation',
      images: [
        {
          url: imageUrl,
          width: 1200,
          height: 630,
          alt: page.data.title,
        },
      ],
    },
    twitter: {
      card: 'summary_large_image',
      title,
      description,
      images: [imageUrl],
    },
  };
}
