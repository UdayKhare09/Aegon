package com.teamzemo.aegon;

import com.teamzemo.aegon.http.RouteGroup;
import com.teamzemo.aegon.http.StatusCode;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;

import static org.junit.jupiter.api.Assertions.*;

public class MiddlewareAndFeaturesTest {

    private static final int PORT = 19081;
    private static AegonServer server;
    private static HttpClient client;

    @BeforeAll
    public static void setUp() {
        client = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(3))
                .build();

        server = AegonServer.create();

        // 1. Global Middleware that sets a tracking header
        server.router().use((ctx, next) -> {
            ctx.set("middleware_timestamp", System.currentTimeMillis());
            ctx.res().header("X-Global-Middleware", "Applied");
            next.call(ctx);
        });

        // 2. Short-circuit middleware on a specific path
        server.router().get("/blocked", ctx -> {
            ctx.res().status(StatusCode.OK).text("Should never be reached");
        });

        // 3. Nested Groups: /api -> /v2 -> /status
        RouteGroup api = server.router().group("/api");
        RouteGroup v2 = api.group("/v2");
        v2.get("/status", ctx -> {
            Long ts = ctx.get("middleware_timestamp");
            assertNotNull(ts);
            ctx.res().status(StatusCode.OK).text("v2-active");
        });

        // 4. Custom Error Handler
        server.router().onError((ctx, errorMsg) -> {
            ctx.res().status(StatusCode.INTERNAL_SERVER_ERROR)
                    .header("X-Error-Handled", "Custom")
                    .text("Handled: " + errorMsg);
        });

        server.router().get("/trigger-error", ctx -> {
            throw new RuntimeException("Intentional crash for testing");
        });

        server.listen(PORT, "127.0.0.1");
        server.runAsync(2);

        for (int i = 0; i < 50; i++) {
            if (server.isRunning()) break;
            try {
                Thread.sleep(50);
            } catch (InterruptedException ignored) {}
        }
    }

    @AfterAll
    public static void tearDown() {
        if (server != null) {
            server.stop();
            server.close();
        }
    }

    @Test
    public void testGlobalMiddlewareApplied() throws Exception {
        HttpRequest req = HttpRequest.newBuilder()
                .uri(URI.create("http://127.0.0.1:" + PORT + "/api/v2/status"))
                .GET()
                .build();

        HttpResponse<String> res = client.send(req, HttpResponse.BodyHandlers.ofString());
        assertEquals(200, res.statusCode());
        assertEquals("v2-active", res.body());
        assertEquals("Applied", res.headers().firstValue("X-Global-Middleware").orElse(""));
    }

    @Test
    public void testCustomErrorHandlerInterceptsException() throws Exception {
        HttpRequest req = HttpRequest.newBuilder()
                .uri(URI.create("http://127.0.0.1:" + PORT + "/trigger-error"))
                .GET()
                .build();

        HttpResponse<String> res = client.send(req, HttpResponse.BodyHandlers.ofString());
        assertEquals(500, res.statusCode());
        assertEquals("Custom", res.headers().firstValue("X-Error-Handled").orElse(""));
        assertTrue(res.body().contains("Intentional crash for testing"));
    }

    @Test
    public void testAsyncMiddlewareWithVirtualThreadBlocking() throws Exception {
        AegonServer s = AegonServer.create();
        int testPort = 19089;
        s.router().useAsync((ctx, next) -> {
            // Simulate blocking call on Java Virtual Thread
            Thread.sleep(50);
            ctx.set("async_mw_user", "alice");
            next.call(ctx);
            // Post-processing on Virtual Thread
            ctx.res().header("X-Async-Middleware", "VirtualThread-Active");
        });

        s.router().get("/async-mw-test", ctx -> {
            String user = ctx.get("async_mw_user");
            ctx.res().status(StatusCode.OK).text("hello-" + user);
        });

        s.listen(testPort, "127.0.0.1");
        s.runAsync(2);
        try {
            for (int i = 0; i < 50; i++) {
                if (s.isRunning()) break;
                Thread.sleep(20);
            }

            HttpRequest req = HttpRequest.newBuilder()
                    .uri(URI.create("http://127.0.0.1:" + testPort + "/async-mw-test"))
                    .GET()
                    .build();

            HttpResponse<String> res = client.send(req, HttpResponse.BodyHandlers.ofString());
            assertEquals(200, res.statusCode());
            assertEquals("hello-alice", res.body());
            assertEquals("VirtualThread-Active", res.headers().firstValue("X-Async-Middleware").orElse(""));
        } finally {
            s.stop();
            s.close();
        }
    }

    @Test
    public void testAsyncMiddlewareShortCircuit() throws Exception {
        AegonServer s = AegonServer.create();
        int testPort = 19090;
        s.router().useAsync((ctx, next) -> {
            Thread.sleep(20);
            if (ctx.req().path().contains("blocked")) {
                ctx.res().status(StatusCode.FORBIDDEN).text("async-blocked");
                return;
            }
            next.call(ctx);
        });

        s.router().get("/blocked", ctx -> {
            ctx.res().status(StatusCode.OK).text("should not reach here");
        });

        s.listen(testPort, "127.0.0.1");
        s.runAsync(2);
        try {
            for (int i = 0; i < 50; i++) {
                if (s.isRunning()) break;
                Thread.sleep(20);
            }

            HttpRequest req = HttpRequest.newBuilder()
                    .uri(URI.create("http://127.0.0.1:" + testPort + "/blocked"))
                    .GET()
                    .build();

            HttpResponse<String> res = client.send(req, HttpResponse.BodyHandlers.ofString());
            assertEquals(403, res.statusCode());
            assertEquals("async-blocked", res.body());
        } finally {
            s.stop();
            s.close();
        }
    }
}

