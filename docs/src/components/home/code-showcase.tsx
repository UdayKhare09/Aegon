'use client';

import { useState } from 'react';
import { motion } from 'motion/react';
import { Copy, Check, Server, Database, Radio, Cpu, Network, ShieldCheck } from 'lucide-react';

interface CodeSnippet {
  id: string;
  title: string;
  description: string;
  icon: typeof Server;
  filename: string;
  code: string;
}

const snippets: CodeSnippet[] = [
  {
    id: 'server',
    title: 'Modern Coroutine Server',
    description: 'Clean C++23 co_await routing with automatic Glaze compile-time JSON binding',
    icon: Server,
    filename: 'server.cpp',
    code: `#include <aegon/http/Server.h>
#include <aegon/core/Task.h>

using namespace aegon;

struct CreateUserRequest {
    std::string username;
    std::string email;
};

struct UserResponse {
    uint64_t id;
    std::string username;
    std::string email;
    std::string status;
};

int main() {
    http::Router router;

    // Coroutine-based REST endpoint with zero callback overhead
    router.post("/api/v1/users", [](http::Context& ctx) -> core::Task<void> {
        auto req_body = ctx.bind_json<CreateUserRequest>();
        if (!req_body) co_return; // 422 if invalid schema

        UserResponse response{
            .id = 1001,
            .username = std::move(req_body->username),
            .email = std::move(req_body->email),
            .status = "active"
        };

        // Serialize via compile-time reflection
        ctx.res().status(http::StatusCode::Created).json(response);
        co_return;
    });

    http::Server server(std::move(router));
    server.listen(8080, "0.0.0.0");
    server.run(std::thread::hardware_concurrency());
    return 0;
}`,
  },
  {
    id: 'orm',
    title: 'Reflection-Free SQL ORM',
    description: 'Type-safe queries with automatic multi-tier L2 in-memory caching',
    icon: Database,
    filename: 'repository.cpp',
    code: `#include <aegon/data/Orm.h>
#include <aegon/core/Task.h>

struct Account {
    int64_t id;
    std::string email;
    double balance;
};

// Automatic compile-time reflection-free mapping
AEGON_MODEL(Account, id, email, balance);

core::Task<Account> get_account(data::Database& db, int64_t id) {
    // Transparently hits L2 cache; falls back to connection pool
    auto account = co_await db.from<Account>()
                              .where("id = ?", id)
                              .cache_for(std::chrono::seconds(60))
                              .first();
    
    co_return account;
}`,
  },
  {
    id: 'websocket',
    title: '63M+ msg/s WebSockets',
    description: 'Low-latency bidirectional streaming across hardware cores',
    icon: Radio,
    filename: 'websocket.cpp',
    code: `#include <aegon/http/Server.h>
#include <aegon/ws/WebSocket.h>

using namespace aegon;

int main() {
    http::Router router;

    router.ws("/live/stream", [](ws::WebSocket& socket) -> core::Task<void> {
        while (auto msg = co_await socket.read()) {
            if (msg->is_text()) {
                // Echo or broadcast back with zero memory copies
                co_await socket.send_text(msg->text());
            }
        }
    });

    http::Server server(std::move(router));
    server.listen(9000);
    server.run();
    return 0;
}`,
  },
];

const highlights = [
  {
    icon: Cpu,
    title: 'Zero Dynamic Allocations',
    description: 'HTTP request contexts and coroutine frames reuse pre-allocated slab arenas sized strictly to L2 CPU cache bounds.',
  },
  {
    icon: Network,
    title: 'Compile-Time Route Map',
    description: 'All routing nodes are evaluated via consteval trie graphs, eliminating runtime string lookups on the critical path.',
  },
  {
    icon: ShieldCheck,
    title: 'Deterministic Cancellation',
    description: 'Async fiber tasks support immediate kernel ring cancellation without leaking file descriptors or socket buffers.',
  },
];

export function CodeShowcase() {
  const [activeTab, setActiveTab] = useState<string>('server');
  const [copied, setCopied] = useState<boolean>(false);

  const activeSnippet = snippets.find((s) => s.id === activeTab) || snippets[0];
  const lines = activeSnippet.code.split('\n');

  const handleCopy = () => {
    navigator.clipboard.writeText(activeSnippet.code);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <section className="py-20 md:py-28 relative">
      <div className="container mx-auto px-4 max-w-6xl">
        <div className="text-center max-w-3xl mx-auto mb-14">
          <div className="inline-flex items-center gap-2 px-3 py-1 rounded-md bg-amber-500/10 border border-amber-500/20 text-amber-300 text-xs font-semibold uppercase tracking-wider mb-3">
            <span>Developer Ergonomics</span>
          </div>
          <h2 className="text-3xl sm:text-4xl md:text-5xl font-extrabold text-white tracking-tight mb-4">
            C++23 Native Async. Without Boilerplate.
          </h2>
          <p className="text-neutral-400 text-base sm:text-lg font-light">
            Zero callback chains, zero thread synchronization overhead, and direct compile-time schema binding.
          </p>
        </div>

        <div className="grid grid-cols-1 lg:grid-cols-12 gap-8 items-start">
          {/* Main IDE Window */}
          <div className="lg:col-span-8 rounded-2xl border border-neutral-800/90 bg-[#0d0d12] shadow-2xl overflow-hidden">
            {/* Window Title Bar */}
            <div className="flex items-center justify-between px-4 py-3 border-b border-neutral-800/80 bg-neutral-900/70">
              <div className="flex items-center gap-3">
                <div className="flex items-center gap-1.5">
                  <span className="w-3 h-3 rounded-full bg-rose-500/80 inline-block" />
                  <span className="w-3 h-3 rounded-full bg-amber-500/80 inline-block" />
                  <span className="w-3 h-3 rounded-full bg-emerald-500/80 inline-block" />
                </div>
                <div className="h-4 w-px bg-neutral-800 hidden sm:block" />
                <div className="flex items-center gap-1 overflow-x-auto aegon-terminal-scrollbar py-0.5">
                  {snippets.map((snippet) => {
                    const Icon = snippet.icon;
                    return (
                      <button
                        key={snippet.id}
                        onClick={() => setActiveTab(snippet.id)}
                        className={`inline-flex items-center gap-1.5 px-3 py-1 rounded-md text-xs font-mono font-medium transition-all ${
                          activeTab === snippet.id
                            ? 'bg-neutral-800 text-amber-300 border border-neutral-700 shadow-sm'
                            : 'text-neutral-400 hover:text-neutral-200 hover:bg-neutral-850'
                        }`}
                      >
                        <Icon className="w-3.5 h-3.5" />
                        <span>{snippet.filename}</span>
                      </button>
                    );
                  })}
                </div>
              </div>

              <button
                onClick={handleCopy}
                className="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-md text-xs font-mono text-neutral-400 hover:text-white hover:bg-neutral-800 transition-colors border border-transparent hover:border-neutral-700 shrink-0 ml-2"
                title="Copy code"
              >
                {copied ? (
                  <>
                    <Check className="w-3.5 h-3.5 text-emerald-400" />
                    <span className="text-emerald-400">Copied</span>
                  </>
                ) : (
                  <>
                    <Copy className="w-3.5 h-3.5" />
                    <span>Copy</span>
                  </>
                )}
              </button>
            </div>

            {/* Description Sub-rail */}
            <div className="px-5 py-2.5 bg-neutral-900/40 border-b border-neutral-800/60 flex items-center justify-between text-xs text-neutral-400 font-mono">
              <span className="truncate">{activeSnippet.description}</span>
              <span className="text-neutral-500 hidden sm:inline ml-2 shrink-0">C++23 ISO</span>
            </div>

            {/* Code Body with Line Numbers */}
            <div className="p-4 sm:p-6 overflow-x-auto aegon-terminal-scrollbar font-mono text-xs sm:text-[13px] leading-relaxed text-neutral-200">
              <pre className="table">
                {lines.map((line, idx) => (
                  <div key={idx} className="table-row">
                    <span className="table-cell pr-4 text-right select-none text-neutral-600 w-8">
                      {idx + 1}
                    </span>
                    <span className="table-cell whitespace-pre">{line}</span>
                  </div>
                ))}
              </pre>
            </div>
          </div>

          {/* Architectural Pillars / Side Callouts */}
          <div className="lg:col-span-4 flex flex-col gap-4">
            {highlights.map((item, idx) => {
              const Icon = item.icon;
              return (
                <motion.div
                  key={item.title}
                  initial={{ opacity: 0, x: 20 }}
                  whileInView={{ opacity: 1, x: 0 }}
                  viewport={{ once: true }}
                  transition={{ duration: 0.5, delay: idx * 0.1 }}
                  className="aegon-card rounded-xl p-5"
                >
                  <div className="flex items-center gap-3 mb-2.5">
                    <div className="p-2 rounded-lg bg-amber-400/10 text-amber-400 border border-amber-400/20">
                      <Icon className="w-4 h-4" />
                    </div>
                    <h3 className="font-bold text-white text-base">
                      {item.title}
                    </h3>
                  </div>
                  <p className="text-neutral-400 text-xs sm:text-sm leading-relaxed font-light">
                    {item.description}
                  </p>
                </motion.div>
              );
            })}
          </div>
        </div>
      </div>
    </section>
  );
}
