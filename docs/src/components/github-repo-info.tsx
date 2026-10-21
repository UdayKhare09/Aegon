'use client';

import { Component, type ReactNode, Suspense } from 'react';
import { GithubInfo } from 'fumadocs-ui/components/github-info';
import { GithubIcon } from '@/components/icons/github';

interface Props {
  owner: string;
  repo: string;
  className?: string;
}

interface State {
  hasError: boolean;
}

class ErrorBoundary extends Component<{ fallback: ReactNode; children: ReactNode }, State> {
  constructor(props: { fallback: ReactNode; children: ReactNode }) {
    super(props);
    this.state = { hasError: false };
  }

  static getDerivedStateFromError() {
    return { hasError: true };
  }

  render() {
    if (this.state.hasError) {
      return this.props.fallback;
    }
    return this.props.children;
  }
}

export function GithubRepoInfo({ owner, repo, className }: Props) {
  const fallback = (
    <a
      href={`https://github.com/${owner}/${repo}`}
      target="_blank"
      rel="noreferrer noopener"
      className="flex flex-col gap-1.5 p-2 rounded-lg text-sm bg-fd-card border border-fd-border/50 text-fd-foreground/80 hover:bg-fd-accent hover:text-fd-accent-foreground transition-colors"
    >
      <p className="flex items-center gap-2 truncate font-medium">
        <GithubIcon className="size-3.5 fill-current" />
        {owner}/{repo}
      </p>
      <div className="flex text-xs items-center gap-1 text-fd-muted-foreground">
        <span>View on GitHub</span>
      </div>
    </a>
  );

  return (
    <ErrorBoundary fallback={fallback}>
      <Suspense
        fallback={
          <div className="flex flex-col gap-1.5 p-2 rounded-lg text-sm bg-fd-card border border-fd-border/50 animate-pulse">
            <div className="flex items-center gap-2">
              <div className="size-3.5 rounded bg-fd-muted" />
              <div className="h-4 w-28 bg-fd-muted rounded" />
            </div>
            <div className="h-3 w-16 bg-fd-muted/60 rounded" />
          </div>
        }
      >
        <GithubInfo
          owner={owner}
          repo={repo}
          className={className}
        />
      </Suspense>
    </ErrorBoundary>
  );
}
