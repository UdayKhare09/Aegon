package com.teamzemo.aegon.examples;

import com.teamzemo.aegon.AegonServer;
import com.teamzemo.aegon.ServerConfig;
import com.teamzemo.aegon.http.Context;
import com.teamzemo.aegon.http.Router;
import com.teamzemo.aegon.http.StatusCode;
import com.teamzemo.aegon.middleware.CorsConfig;
import com.teamzemo.aegon.middleware.SecurityHeadersConfig;

import java.util.Map;

/**
 * Quickstart showcase demonstrating the high-performance Aegon Java 22+ API.
 *
 * <p>Features highlighted:
 * <ul>
 *   <li>Native reactor fast-path routes (0 coroutines, 0 thread switches)</li>
 *   <li>Project Loom Virtual Thread coroutine dispatch via {@code getAsync} / {@code postAsync}</li>
 *   <li>Zero-overhead JSON serialization and deserialization via Fastjson2 records</li>
 *   <li>Path parameters and query string parsing</li>
 *   <li>CORS and Security Headers middleware</li>
 *   <li>Real-time Server-Sent Events (SSE) stream</li>
 *   <li>RFC 6455 bidirectional WebSockets</li>
 * </ul>
 */
public final class QuickstartServer {

    /**
     * Strongly typed payload record.
     */
    public record Greeting(String message, String version, long timestamp) {}

    /**
     * User registration request record.
     */
    public record RegisterUserRequest(String username, String email) {}

    public static void main(String[] args) {
        // 1. Configure server with thread-per-core reactor workers
        ServerConfig config = ServerConfig.builder()
                .ringEntries(2048)
                .bufferPoolEntries(4096)
                .bufferSize(4096)
                .build();

        AegonServer server = AegonServer.create(config);
        Router router = server.router();

        // 2. Global middleware
        router.useCors(CorsConfig.builder()
                .allowAllOrigins(true)
                .allowMethods("GET", "POST", "PUT", "DELETE", "OPTIONS")
                .allowHeaders("Content-Type", "Authorization")
                .build());

        router.useSecurityHeaders(SecurityHeadersConfig.builder()
                .xContentTypeOptions(true)
                .xFrameOptions("DENY")
                .contentSecurityPolicy("default-src 'self'")
                .build());

        // 3. Reactor Fast-Path Sync Routes (Runs inline on native worker thread)
        router.get("/", ctx -> {
            ctx.text("Welcome to Aegon Java!");
        });

        router.get("/health", ctx -> {
            ctx.json(Map.of("status", "UP", "engine", "Aegon C ABI + Java 22 FFM"));
        });

        // 4. Project Loom Virtual Thread Async Routes (Seamless blocking I/O)
        router.getAsync("/api/v1/info", ctx -> {
            // Running on a dedicated Project Loom Virtual Thread!
            // Safe to execute blocking DB queries, file I/O, or remote HTTP calls:
            Thread.sleep(10);
            ctx.json(new Greeting("Hello from Virtual Thread!", "0.2.1", System.currentTimeMillis()));
        });

        router.postAsync("/api/v1/users", ctx -> {
            // Parse strongly-typed JSON request body
            RegisterUserRequest req = ctx.bindJson(RegisterUserRequest.class);
            if (req.username() == null || req.username().isBlank()) {
                ctx.problem(StatusCode.BAD_REQUEST, "Validation Error", "Field 'username' is required");
                return;
            }

            // Return 201 Created
            ctx.status(StatusCode.CREATED)
               .json(Map.of("id", 1001, "username", req.username(), "email", req.email()));
        });

        // 5. Hierarchical Route Groups & Path Parameters
        router.group("/api/v1/items", group -> {
            group.get("/:id", ctx -> {
                String id = ctx.param("id");
                String querySort = ctx.query("sort");
                ctx.json(Map.of("itemId", id, "sort", querySort != null ? querySort : "default"));
            });

            group.del("/:id", ctx -> {
                String id = ctx.param("id");
                ctx.status(StatusCode.NO_CONTENT);
            });
        });

        // 6. Real-time Server-Sent Events (SSE)
        router.sse("/events/ticks", (ctx, sse) -> {
            sse.sendComment("connected");
            for (int i = 1; i <= 5; i++) {
                Thread.sleep(500);
                sse.sendEvent("tick", "Tick #" + i + " at " + System.currentTimeMillis());
            }
            sse.close();
        });

        // 7. Real-time RFC 6455 WebSockets
        router.ws("/ws/echo", session -> {
            session.onOpen(ws -> ws.sendText("Echo WebSocket connected!"));
            session.onText((ws, message) -> ws.sendText("Echo: " + message));
        });

        // 8. Bind listener and run
        int port = 8080;
        server.listen(port, "0.0.0.0");
        System.out.printf("Starting Aegon Java Quickstart server on http://0.0.0.0:%d%n", port);

        // Blocks current thread until SIGINT / SIGTERM
        server.run(Runtime.getRuntime().availableProcessors());
    }
}

