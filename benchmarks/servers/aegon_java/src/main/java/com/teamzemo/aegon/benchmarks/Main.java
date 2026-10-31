package com.teamzemo.aegon.benchmarks;

import com.alibaba.fastjson2.JSON;
import com.teamzemo.aegon.AegonServer;
import com.teamzemo.aegon.ServerConfig;
import com.teamzemo.aegon.http.Context;
import com.teamzemo.aegon.http.Router;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public final class Main {

    public record Rating(long score, long count) {}

    public record DatasetItem(
        long id,
        String name,
        String category,
        long price,
        long quantity,
        boolean active,
        List<String> tags,
        Rating rating
    ) {}

    public record ProcessedItem(
        long id,
        String name,
        String category,
        long price,
        long quantity,
        long total,
        boolean active,
        List<String> tags,
        Rating rating
    ) {}

    public record JsonPayload(int count, List<ProcessedItem> items) {}

    private static List<DatasetItem> dataset = new ArrayList<>();

    private static void loadDataset() {
        List<String> paths = List.of(
            "benchmarks/data/dataset.json",
            "data/dataset.json",
            "../data/dataset.json",
            "../../data/dataset.json",
            "/data/dataset.json"
        );

        String content = null;
        for (String p : paths) {
            Path path = Path.of(p);
            if (Files.exists(path)) {
                try {
                    content = Files.readString(path);
                    break;
                } catch (IOException ignored) {}
            }
        }

        if (content == null || content.isBlank()) {
            System.err.println("[Aegon Java] Warning: dataset.json not found, initializing synthetic dataset");
            for (long i = 1; i <= 100; i++) {
                dataset.add(new DatasetItem(
                    i,
                    "Item " + i,
                    "benchmark",
                    100 + i,
                    10 + (i % 50),
                    (i % 2 == 0),
                    List.of("tag1", "benchmark"),
                    new Rating(95, 100)
                ));
            }
            return;
        }

        try {
            dataset = JSON.parseArray(content, DatasetItem.class);
        } catch (Exception e) {
            System.err.println("[Aegon Java] Failed to parse dataset.json: " + e.getMessage());
        }
    }

    private static long parseQueryLong(Context ctx, String key) {
        String val = ctx.query(key);
        if (val != null && !val.isEmpty()) {
            try {
                return Long.parseLong(val);
            } catch (NumberFormatException ignored) {}
        }
        return 0;
    }

    public static void main(String[] args) {
        int workers = 2;
        int basePort = 18090;
        String certFile = "benchmarks/certs/server.crt";
        String keyFile = "benchmarks/certs/server.key";

        if (args.length > 0) {
            try { workers = Integer.parseInt(args[0]); } catch (NumberFormatException ignored) {}
        }
        if (args.length > 1) {
            try { basePort = Integer.parseInt(args[1]); } catch (NumberFormatException ignored) {}
        }
        if (args.length > 2) {
            certFile = args[2];
        }
        if (args.length > 3) {
            keyFile = args[3];
        }

        if (!Files.exists(Path.of(certFile)) || !Files.exists(Path.of(keyFile))) {
            if (Files.exists(Path.of("certs/server.crt")) && Files.exists(Path.of("certs/server.key"))) {
                certFile = "certs/server.crt";
                keyFile = "certs/server.key";
            }
        }

        loadDataset();

        ServerConfig config = ServerConfig.builder()
                .ringEntries(8192)
                .bufferPoolEntries(16384)
                .bufferSize(4096)
                .enableHttp3(true)
                .tcpNoDelay(true)
                .tcpKeepAlive(true)
                .build();

        AegonServer server = AegonServer.create(config);
        Router router = server.router();

        // 1. HttpArena Baseline (/baseline GET and POST)
        router.get("/baseline", ctx -> {
            long sum = parseQueryLong(ctx, "a") + parseQueryLong(ctx, "b");
            ctx.text(Long.toString(sum));
        });

        router.post("/baseline", ctx -> {
            long sum = parseQueryLong(ctx, "a") + parseQueryLong(ctx, "b");
            String body = ctx.body();
            if (body != null && !body.isBlank()) {
                try {
                    sum += Long.parseLong(body.trim());
                } catch (NumberFormatException ignored) {}
            }
            ctx.text(Long.toString(sum));
        });

        // 2. HttpArena Pipeline (/pipeline GET)
        router.get("/pipeline", ctx -> {
            ctx.text("ok");
        });

        // 3. HttpArena Dynamic JSON (/json/:count GET)
        router.get("/json/:count", ctx -> {
            int count = 0;
            String countStr = ctx.param("count");
            if (countStr != null && !countStr.isEmpty()) {
                try {
                    count = Integer.parseInt(countStr);
                } catch (NumberFormatException ignored) {}
            }
            long m = 1;
            String mStr = ctx.query("m");
            if (mStr != null && !mStr.isEmpty()) {
                try {
                    m = Long.parseLong(mStr);
                } catch (NumberFormatException ignored) {}
            }

            int n = Math.min(count, dataset.size());
            List<ProcessedItem> items = new ArrayList<>(n);
            for (int i = 0; i < n; i++) {
                DatasetItem d = dataset.get(i);
                items.add(new ProcessedItem(
                    d.id(),
                    d.name(),
                    d.category(),
                    d.price(),
                    d.quantity(),
                    d.price() * d.quantity() * m,
                    d.active(),
                    d.tags(),
                    d.rating()
                ));
            }

            ctx.json(new JsonPayload(n, items));
        });

        // 4. HttpArena Async Timer (/delay/:ms GET via Project Loom)
        router.getAsync("/delay/:ms", ctx -> {
            long ms = 0;
            String msStr = ctx.param("ms");
            if (msStr != null && !msStr.isEmpty()) {
                try {
                    ms = Long.parseLong(msStr);
                } catch (NumberFormatException ignored) {}
            }
            if (ms > 0) {
                try {
                    Thread.sleep(ms);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            }
            ctx.text(Long.toString(ms));
        });

        // 5. HttpArena Echo (/echo POST)
        router.post("/echo", ctx -> {
            ctx.res().header("Content-Type", "application/octet-stream");
            ctx.res().body(ctx.bodyBytes());
        });

        // 6. HttpArena WebSocket Echo (/ws)
        router.wsEcho("/ws");

        // 7. HttpArena Static Files (/static/*)
        List<String> staticPaths = List.of(
            "benchmarks/data/static",
            "data/static",
            "../data/static",
            "../../data/static",
            "/data/static"
        );
        for (String sp : staticPaths) {
            Path p = Path.of(sp);
            if (Files.exists(p) && Files.isDirectory(p)) {
                router.staticFiles("/static", p.toAbsolutePath().toString());
                break;
            }
        }

        int portPlain = basePort;
        int portTls   = basePort + 1;
        int portH2    = basePort + 2;
        int portH3    = basePort + 3;

        server.listen(portPlain);

        boolean hasCerts = Files.exists(Path.of(certFile)) && Files.exists(Path.of(keyFile));
        if (hasCerts) {
            server.enableTls(certFile, keyFile);
            server.listenTls(portTls);
            server.listenTls(portH2);
            server.listenTls(portH3);
        }

        System.out.println("===================================================================");
        System.out.println(" [Aegon Java Server] Running with " + workers + " worker threads (HttpArena Spec)");
        System.out.println("   -> Plain HTTP/1.1 & WS : http://0.0.0.0:" + portPlain);
        if (hasCerts) {
            System.out.println("   -> TLS 1.3 HTTP/1.1    : https://0.0.0.0:" + portTls);
            System.out.println("   -> TLS 1.3 HTTP/2      : https://0.0.0.0:" + portH2);
            System.out.println("   -> TLS 1.3 HTTP/3 QUIC : https://0.0.0.0:" + portH3);
        }
        System.out.println("===================================================================");
        System.out.flush();

        server.run(workers);
    }
}

