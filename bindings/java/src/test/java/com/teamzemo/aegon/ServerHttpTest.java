package com.teamzemo.aegon;

import com.teamzemo.aegon.http.Context;
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

public class ServerHttpTest {

    private static final int PORT = 19080;
    private static AegonServer server;
    private static HttpClient client;

    public record UserDto(long id, String name, String email) {}
    public record CreateUserRequest(String name, String email) {}

    @BeforeAll
    public static void setUp() {
        client = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(3))
                .build();

        server = AegonServer.create();

        // 1. Synchronous Plaintext Fast-Path
        server.router().get("/health", ctx -> {
            ctx.res().status(StatusCode.OK).text("OK");
        });

        // 2. Synchronous Path & Query Parameter extraction
        server.router().get("/users/:id", ctx -> {
            String id = ctx.req().param("id").orElse("");
            String format = ctx.req().query("format").orElse("text");
            ctx.res().status(StatusCode.OK).text("id=" + id + ",format=" + format);
        });

        // 3. Hierarchical Route Group
        RouteGroup api = server.router().group("/api/v1");

        // 4. Automated Project Loom Virtual Thread Async Route
        api.postAsync("/users", ctx -> {
            // Verify execution is automatically running on a Java Virtual Thread!
            boolean isVirtual = Thread.currentThread().isVirtual();

            CreateUserRequest req = ctx.bindJson(CreateUserRequest.class);
            if (req == null || req.name() == null) {
                ctx.problem(StatusCode.BAD_REQUEST, "Invalid Request", "name is required");
                return;
            }

            // Simulate blocking call (completely safe on Virtual Thread)
            Thread.sleep(10);

            UserDto created = new UserDto(42L, req.name(), req.email());
            ctx.res().status(StatusCode.CREATED)
                    .header("X-Is-Virtual", String.valueOf(isVirtual))
                    .json(created);
        });

        // 5. Delete route
        api.del("/users/:id", ctx -> {
            String id = ctx.req().param("id").orElse("");
            ctx.res().status(StatusCode.NO_CONTENT).header("X-Deleted", id);
        });

        // 6. RFC 7807 Problem Details
        server.router().get("/problem-test", ctx -> {
            ctx.problem(StatusCode.NOT_FOUND, "User Not Found", "The user with id 999 was not found");
        });

        // Bind port and start in background
        server.listen(PORT, "127.0.0.1");
        server.runAsync(2);

        // Wait briefly for server startup
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
    public void testSyncHealthRoute() throws Exception {
        HttpRequest req = HttpRequest.newBuilder()
                .uri(URI.create("http://127.0.0.1:" + PORT + "/health"))
                .GET()
                .build();

        HttpResponse<String> res = client.send(req, HttpResponse.BodyHandlers.ofString());
        assertEquals(200, res.statusCode());
        assertEquals("OK", res.body());
    }

    @Test
    public void testParamAndQueryExtraction() throws Exception {
        HttpRequest req = HttpRequest.newBuilder()
                .uri(URI.create("http://127.0.0.1:" + PORT + "/users/123?format=json"))
                .GET()
                .build();

        HttpResponse<String> res = client.send(req, HttpResponse.BodyHandlers.ofString());
        assertEquals(200, res.statusCode());
        assertEquals("id=123,format=json", res.body());
    }

    @Test
    public void testVirtualThreadAsyncJsonRoute() throws Exception {
        String jsonPayload = "{\"name\":\"Surendra\",\"email\":\"surendra@teamzemo.com\"}";
        HttpRequest req = HttpRequest.newBuilder()
                .uri(URI.create("http://127.0.0.1:" + PORT + "/api/v1/users"))
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(jsonPayload))
                .build();

        HttpResponse<String> res = client.send(req, HttpResponse.BodyHandlers.ofString());
        assertEquals(201, res.statusCode());

        // Verify it ran on Project Loom Virtual Thread!
        assertTrue(res.headers().firstValue("X-Is-Virtual").isPresent());
        assertEquals("true", res.headers().firstValue("X-Is-Virtual").get());

        // Verify JSON response body
        assertTrue(res.body().contains("\"name\":\"Surendra\""));
        assertTrue(res.body().contains("\"id\":42"));
    }

    @Test
    public void testDeleteRoute() throws Exception {
        HttpRequest req = HttpRequest.newBuilder()
                .uri(URI.create("http://127.0.0.1:" + PORT + "/api/v1/users/42"))
                .DELETE()
                .build();

        HttpResponse<String> res = client.send(req, HttpResponse.BodyHandlers.ofString());
        assertEquals(204, res.statusCode());
        assertEquals("42", res.headers().firstValue("X-Deleted").orElse(""));
    }

    @Test
    public void testProblemDetails() throws Exception {
        HttpRequest req = HttpRequest.newBuilder()
                .uri(URI.create("http://127.0.0.1:" + PORT + "/problem-test"))
                .GET()
                .build();

        HttpResponse<String> res = client.send(req, HttpResponse.BodyHandlers.ofString());
        assertEquals(404, res.statusCode());
        assertTrue(res.headers().firstValue("Content-Type").orElse("").contains("application/problem+json"));
        assertTrue(res.body().contains("User Not Found"));
        assertTrue(res.body().contains("999"));
    }
}

