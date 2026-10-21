import type { Metadata } from 'next';
import { Inter } from 'next/font/google';
import { Provider } from '@/components/provider';
import './global.css';

const inter = Inter({
  subsets: ['latin'],
});

export const metadata: Metadata = {
  title: {
    template: '%s | AEGON',
    default: 'AEGON | High-Performance C++23 Framework',
  },
  description: 'High-Performance C++23 Asynchronous Web & Data Framework',
  icons: {
    icon: [
      { url: '/a.svg', type: 'image/svg+xml' },
    ],
  },
};

export default function Layout({ children }: LayoutProps<'/'>) {
  return (
    <html lang="en" className={`${inter.className} dark`} style={{ colorScheme: 'dark' }} suppressHydrationWarning>
      <body className="flex flex-col min-h-screen bg-neutral-950 text-neutral-100">
        <Provider>{children}</Provider>
      </body>
    </html>
  );
}
