package com.teamzemo.aegon;

import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;
import java.net.URI;
import java.net.http.HttpClient;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.*;

public class StreamingTest {

    private static final int PORT = 19082;
    private static AegonServer server;

    @BeforeAll
    public static void setUp() {
        server = AegonServer.create();

        // 1. WebSocket Echo endpoint
        server.router().wsEcho("/ws/echo");

        // 2. Custom WebSocket handler endpoint
        server.router().ws("/ws/chat", session -> {
            session.onOpen(ws -> {
                ws.sendText("chat_ready");
            });
            session.onText((ws, text) -> {
                ws.sendText("echo: " + text);
            });
        });

        // 3. Server-Sent Events (SSE) streaming endpoint
        server.router().sse("/events", (ctx, sse) -> {
            Thread.sleep(30);
            sse.sendComment("keepalive");
            sse.sendEvent("welcome", "Connected to Aegon SSE");
            sse.sendData("Tick 1");
            Thread.sleep(30);
            sse.close();
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
    public void testServerSentEvents() throws Exception {
        try (Socket sock = new Socket("127.0.0.1", PORT)) {
            sock.setSoTimeout(3000);
            OutputStream out = sock.getOutputStream();
            InputStream in = sock.getInputStream();

            String req = "GET /events HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
            out.write(req.getBytes(StandardCharsets.UTF_8));
            out.flush();

            StringBuilder sb = new StringBuilder();
            byte[] buf = new byte[2048];
            int read;
            while ((read = in.read(buf)) != -1) {
                sb.append(new String(buf, 0, read, StandardCharsets.UTF_8));
                if (sb.toString().contains("Tick 1")) {
                    break;
                }
            }

            String response = sb.toString();
            assertTrue(response.contains("200 OK"));
            assertTrue(response.contains("Content-Type: text/event-stream") || response.contains("content-type: text/event-stream"));
            assertTrue(response.contains("event: welcome"));
            assertTrue(response.contains("data: Connected to Aegon SSE"));
            assertTrue(response.contains("data: Tick 1"));
        }
    }

    @Test
    public void testWebSocketEchoDirect() throws Exception {
        // Direct socket test to verify RFC 6455 frame exchange on /ws/echo
        try (Socket sock = new Socket("127.0.0.1", PORT)) {
            sock.setSoTimeout(3000);
            OutputStream out = sock.getOutputStream();
            InputStream in = sock.getInputStream();

            String handshake =
                    "GET /ws/echo HTTP/1.1\r\n" +
                    "Host: localhost\r\n" +
                    "Upgrade: websocket\r\n" +
                    "Connection: Upgrade\r\n" +
                    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n" +
                    "Sec-WebSocket-Version: 13\r\n\r\n";

            out.write(handshake.getBytes(StandardCharsets.UTF_8));
            out.flush();

            byte[] buf = new byte[2048];
            int read = in.read(buf);
            assertTrue(read > 0);
            String response = new String(buf, 0, read, StandardCharsets.UTF_8);
            assertTrue(response.contains("101 Switching Protocols"));
            assertTrue(response.contains("Sec-WebSocket-Accept"));

            // Send masked text frame: "Hello Aegon"
            byte[] mask = new byte[]{0x12, 0x34, 0x56, 0x78};
            byte[] textBytes = "Hello Aegon".getBytes(StandardCharsets.UTF_8);
            byte[] frame = new byte[6 + textBytes.length];
            frame[0] = (byte) 0x81; // FIN + Text
            frame[1] = (byte) (0x80 | textBytes.length); // Masked
            System.arraycopy(mask, 0, frame, 2, 4);
            for (int i = 0; i < textBytes.length; i++) {
                frame[6 + i] = (byte) (textBytes[i] ^ mask[i % 4]);
            }
            out.write(frame);
            out.flush();

            // Receive unmasked echo frame from server
            read = in.read(buf);
            assertTrue(read >= 2);
            assertEquals((byte) 0x81, buf[0]);
            int len = buf[1] & 0x7F;
            assertEquals(textBytes.length, len);
            String echoed = new String(buf, 2, len, StandardCharsets.UTF_8);
            assertEquals("Hello Aegon", echoed);
        }
    }

    @Test
    public void testCustomWebSocketSession() throws Exception {
        LinkedBlockingQueue<String> messages = new LinkedBlockingQueue<>();
        HttpClient client = HttpClient.newHttpClient();

        CompletableFuture<java.net.http.WebSocket> wsFuture = client.newWebSocketBuilder()
                .buildAsync(URI.create("ws://127.0.0.1:" + PORT + "/ws/chat"), new java.net.http.WebSocket.Listener() {
                    @Override
                    public java.util.concurrent.CompletionStage<?> onText(java.net.http.WebSocket webSocket, CharSequence data, boolean last) {
                        messages.add(data.toString());
                        webSocket.request(1);
                        return null;
                    }
                });

        java.net.http.WebSocket ws = wsFuture.get(3, TimeUnit.SECONDS);

        // 1. First message should be the onOpen greeting: "chat_ready"
        String greeting = messages.poll(3, TimeUnit.SECONDS);
        assertEquals("chat_ready", greeting);

        // 2. Send text and verify custom echo handler
        ws.sendText("Testing Aegon WS", true);
        String reply = messages.poll(3, TimeUnit.SECONDS);
        assertEquals("echo: Testing Aegon WS", reply);

        ws.sendClose(java.net.http.WebSocket.NORMAL_CLOSURE, "bye").get(3, TimeUnit.SECONDS);
        Thread.sleep(50);
    }
}
