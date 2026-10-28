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
}

