'use client';

import { useState } from 'react';
import { motion } from 'motion/react';
import { Copy, Check, Code2, Terminal, Server, Database, Radio } from 'lucide-react';

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
    filename: 'main.cpp',
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
        if (!req_body) co_return; // 422 if invalid

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

// Automatic reflection-free mapping
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
}`,
  },
];

export function CodeShowcase() {
  const [activeTab, setActiveTab] = useState(snippets[0].id);
  const [copied, setCopied] = useState(false);

  const activeSnippet = snippets.find((s) => s.id === activeTab) || snippets[0];

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
            <Code2 className="w-3.5 h-3.5 text-amber-400" />
            <span>Ergonomic Developer Experience</span>
          </div>
          <h2 className="text-3xl sm:text-4xl md:text-5xl font-extrabold text-white tracking-tight mb-4">
            C++23 Modernity Without the Boilerplate
          </h2>
          <p className="text-neutral-400 text-base sm:text-lg">
            Say goodbye to nested callbacks, raw pointer spaghetti, and complex thread locking.
            Aegon leverages native <code className="text-amber-300 font-mono">co_await</code> to write synchronous-looking, ultra-fast asynchronous code.
          </p>
        </div>

        {/* Tab Selection */}
        <div className="flex flex-wrap justify-center gap-3 mb-8">
          {snippets.map((snippet) => {
            const Icon = snippet.icon;
            const isActive = snippet.id === activeTab;
            return (
              <button
                key={snippet.id}
                onClick={() => setActiveTab(snippet.id)}
                className={`flex items-center gap-2.5 px-5 py-2.5 rounded-xl text-sm font-medium transition-all ${
                  isActive
                    ? 'bg-amber-400/20 text-amber-300 border border-amber-400/40 shadow-[0_0_15px_rgba(255,223,102,0.15)]'
                    : 'bg-neutral-900/60 text-neutral-400 border border-neutral-800 hover:text-white hover:bg-neutral-800'
                }`}
              >
                <Icon className="w-4 h-4" />
                <span>{snippet.title}</span>
              </button>
            );
          })}
        </div>

        {/* Code Card Box */}
        <div className="max-w-4xl mx-auto rounded-2xl overflow-hidden border border-neutral-800/80 bg-neutral-950/90 shadow-2xl backdrop-blur-xl">
          {/* Top Window Bar */}
          <div className="flex items-center justify-between px-5 py-3.5 bg-neutral-900/80 border-b border-neutral-800/80">
            <div className="flex items-center gap-2">
              <span className="w-3 h-3 rounded-full bg-red-500/80" />
              <span className="w-3 h-3 rounded-full bg-yellow-500/80" />
              <span className="w-3 h-3 rounded-full bg-emerald-500/80" />
              <span className="ml-3 text-xs font-mono text-neutral-400">
                {activeSnippet.filename}
              </span>
            </div>

            <div className="flex items-center gap-3">
              <span className="text-xs text-neutral-500 hidden sm:inline">
                {activeSnippet.description}
              </span>
              <button
                onClick={handleCopy}
                className="flex items-center gap-1.5 px-3 py-1 rounded-lg text-xs font-medium text-neutral-300 hover:text-white bg-neutral-800 hover:bg-neutral-700 transition-colors"
                title="Copy code"
              >
                {copied ? (
                  <>
                    <Check className="w-3.5 h-3.5 text-emerald-400" />
                    <span className="text-emerald-400">Copied!</span>
                  </>
                ) : (
                  <>
                    <Copy className="w-3.5 h-3.5" />
                    <span>Copy</span>
                  </>
                )}
              </button>
            </div>
          </div>

          {/* Code Area */}
          <div className="p-6 overflow-x-auto text-sm font-mono text-neutral-200 leading-relaxed max-h-[520px]">
            <pre className="selection:bg-amber-400/30">
              <code>{activeSnippet.code}</code>
            </pre>
          </div>
        </div>
      </div>
    </section>
  );
}
