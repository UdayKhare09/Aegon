'use client';

import { useEffect, useId, useState } from 'react';
import { useTheme } from 'next-themes';

export function Mermaid({ chart }: { chart: string }) {
  const id = useId();
  const safeId = 'mermaid-' + id.replace(/[^a-zA-Z0-9]/g, '');
  const { resolvedTheme } = useTheme();
  const [svg, setSvg] = useState<string>('');
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let active = true;

    async function renderChart() {
      try {
        const { default: mermaid } = await import('mermaid');
        const isDark = resolvedTheme === 'dark';

        mermaid.initialize({
          startOnLoad: false,
          securityLevel: 'loose',
          fontFamily: 'inherit',
          theme: 'base',
          themeVariables: isDark
            ? {
                darkMode: true,
                background: 'transparent',
                mainBkg: '#1c1c21',
                nodeBorder: '#3f3f46',
                textColor: '#f4f4f5',
                lineColor: '#71717a',
                primaryColor: '#27272f',
                primaryBorderColor: '#FFDF66',
                primaryTextColor: '#f4f4f5',
                secondaryColor: '#1c1c21',
                secondaryBorderColor: '#3f3f46',
                secondaryTextColor: '#d4d4d8',
                tertiaryColor: '#18181b',
                tertiaryBorderColor: '#27272a',
                tertiaryTextColor: '#a1a1aa',
                edgeLabelBackground: '#18181b',
                labelBoxBkgColor: '#18181b',
                labelBoxBorderColor: '#3f3f46',
                labelTextColor: '#d4d4d8',
                clusterBkg: 'rgba(24, 24, 27, 0.5)',
                clusterBorder: '#3f3f46',
                fontFamily: 'inherit',
                fontSize: '12px',
              }
            : {
                darkMode: false,
                background: 'transparent',
                mainBkg: '#ffffff',
                nodeBorder: '#cbd5e1',
                textColor: '#0f172a',
                lineColor: '#64748b',
                primaryColor: '#f8fafc',
                primaryBorderColor: '#eab308',
                primaryTextColor: '#0f172a',
                secondaryColor: '#ffffff',
                secondaryBorderColor: '#cbd5e1',
                secondaryTextColor: '#334155',
                tertiaryColor: '#f8fafc',
                tertiaryBorderColor: '#e2e8f0',
                tertiaryTextColor: '#64748b',
                edgeLabelBackground: '#ffffff',
                labelBoxBkgColor: '#ffffff',
                labelBoxBorderColor: '#cbd5e1',
                labelTextColor: '#475569',
                clusterBkg: '#f8fafc',
                clusterBorder: '#cbd5e1',
                fontFamily: 'inherit',
                fontSize: '12px',
              },
          themeCSS: `
            .node rect, .node circle, .node polygon { rx: 8px !important; ry: 8px !important; stroke-width: 1.5px !important; }
            .edgeLabel { font-size: 11px !important; font-weight: 500 !important; }
            .edgeLabel rect { fill: ${isDark ? '#18181b' : '#ffffff'} !important; opacity: 0.95; rx: 4px; ry: 4px; }
            .cluster rect { rx: 12px !important; ry: 12px !important; stroke-dasharray: 4 4; }
          `,
        });

        const { svg: renderedSvg } = await mermaid.render(
          safeId,
          chart.replaceAll('\\n', '\n')
        );

        if (active) {
          setSvg(renderedSvg);
          setError(null);
        }
      } catch (err: any) {
        if (active) {
          setError(err?.message || String(err));
        }
      }
    }

    renderChart();

    return () => {
      active = false;
    };
  }, [chart, resolvedTheme, safeId]);

  if (error) {
    return (
      <div className="my-4 rounded-xl border border-red-500/30 bg-red-500/10 p-4 text-xs text-red-400">
        <p className="font-semibold">Mermaid rendering error:</p>
        <pre className="mt-1 whitespace-pre-wrap">{error}</pre>
      </div>
    );
  }

  if (!svg) {
    return <div className="my-6 h-32 animate-pulse rounded-xl border bg-fd-card/50" />;
  }

  return (
    <div
      className="my-6 flex justify-center overflow-x-auto rounded-xl border bg-fd-card/80 p-4 backdrop-blur-sm [&>svg]:max-w-full [&>svg]:h-auto [&>svg]:max-h-[340px]"
      dangerouslySetInnerHTML={{ __html: svg }}
    />
  );
}
