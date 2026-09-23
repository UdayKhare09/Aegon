import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'Aegon',
  description: 'Linux-Native C++26 Asynchronous Web Framework & Compile-Time ORM',
  cleanUrls: true,
  lastUpdated: true,
  base: process.env.BASE_PATH || '/',

  themeConfig: {
    siteTitle: 'Aegon',
    logo: '/logo.svg',

    nav: [
      { text: 'Home', link: '/' },
      { text: 'Guide', link: '/guide/getting-started' },
      { text: 'Benchmarks', link: '/guide/benchmarks' },
      {
        text: 'GitHub',
        link: 'https://github.com/UdayKhare09/Aegon'
      }
    ],

    sidebar: {
      '/guide/': [
        {
          text: 'Getting Started',
          items: [
            { text: 'Getting Started', link: '/guide/getting-started' },
            { text: 'CLI Tooling (aegon)', link: '/guide/cli' },
            { text: 'Performance Benchmarks', link: '/guide/benchmarks' },
          ]
        },
        {
          text: 'HTTP Framework',
          items: [
            { text: 'Routing & Groups', link: '/guide/routing' },
            { text: 'HTTP Request', link: '/guide/request' },
            { text: 'HTTP Response', link: '/guide/response' },
            { text: 'Context & Binding', link: '/guide/context' },
            { text: 'Server Lifecycle', link: '/guide/server' },
            { text: 'Error Handling', link: '/guide/error-handling' },
            { text: 'Middleware Pipeline', link: '/guide/middleware' },
            {
              text: 'Built-in Middleware',
              collapsed: false,
              items: [
                { text: 'CORS', link: '/guide/cors' },
                { text: 'Security Headers', link: '/guide/security-headers' },
                { text: 'Authentication & RBAC', link: '/guide/auth' },
              ]
            },
            { text: 'JWT Engine', link: '/guide/jwt' },
            { text: 'HTTP Client', link: '/guide/http-client' },
            { text: 'API Gateway & Reverse Proxy', link: '/guide/gateway' },
            { text: 'WebSocket (RFC 6455)', link: '/guide/websocket' },
          ]
        },
        {
          text: 'Services & Validation',
          items: [
            { text: 'Configuration (YAML)', link: '/guide/config' },
            { text: 'Service Registry (DI)', link: '/guide/service-registry' },
            { text: 'Data Validation', link: '/guide/validation' },
          ]
        },
        {
          text: 'Data — Core Types',
          items: [
            { text: 'Foundational Data Types', link: '/guide/data/types' },
          ]
        },
        {
          text: 'Data — SQL Engine & ORM',
          items: [
            { text: 'Database Config & Drivers', link: '/guide/data/sql/config' },
            { text: 'Schema Definition', link: '/guide/data/sql/schema' },
            { text: 'Querying', link: '/guide/data/sql/querying' },
            { text: 'Mutations & OCC', link: '/guide/data/sql/mutations' },
            { text: 'Relationships', link: '/guide/data/sql/relations' },
            { text: 'Transactions & Consistency', link: '/guide/data/sql/transactions' },
            { text: 'Query & Entity Caching', link: '/guide/data/sql/cache' },
          ]
        },
        {
          text: 'Data — Native Async Redis',
          items: [
            { text: 'Client & Commands', link: '/guide/data/redis/client' },
            { text: 'Topologies (Sentinel / Cluster)', link: '/guide/data/redis/topologies' },
            { text: 'Advanced (Locks, Pipelines, Streams)', link: '/guide/data/redis/advanced' },
          ]
        },
        {
          text: 'Data — MemStore (In-Process Cache)',
          items: [
            { text: 'MemStore Guide', link: '/guide/data/memory/memstore' },
          ]
        },
        {
          text: 'Advanced / Internals',
          collapsed: true,
          items: [
            { text: 'Task & Coroutines', link: '/guide/core/task' },
            { text: 'io_uring Subsystem', link: '/guide/core/io-uring' },
            { text: 'BufferPool', link: '/guide/core/buffer-pool' },
            { text: 'EventLoop', link: '/guide/core/event-loop' },
          ]
        }
      ]
    },

    search: {
      provider: 'local'
    },

    socialLinks: [
      { icon: 'github', link: 'https://github.com/UdayKhare09/Aegon' }
    ],

    footer: {
      message: 'Released under the MIT License.',
      copyright: 'Copyright © 2026 Aegon Framework'
    }
  }
})
