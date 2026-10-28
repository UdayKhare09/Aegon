package com.teamzemo.aegon.examples;

import com.teamzemo.aegon.AegonServer;
import com.teamzemo.aegon.ws.WebSocket;

import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Real-time WebSocket multi-user chat room server.
 */
public final class WebSocketChatServer {

    // Track active connected clients
    private static final Set<WebSocket> CLIENTS = ConcurrentHashMap.newKeySet();

    public static void main(String[] args) {
        AegonServer server = AegonServer.create();

        server.router().ws("/chat", session -> {
            session.onOpen(ws -> {
                CLIENTS.add(ws);
                broadcast("[System] A new user joined the chat. Active users: " + CLIENTS.size());
            });

            session.onText((ws, message) -> {
                broadcast("[User] " + message);
            });

            session.onClose((ws, code, reason) -> {
                CLIENTS.remove(ws);
                broadcast("[System] A user left the chat. Active users: " + CLIENTS.size());
            });

            session.onError((ws, error) -> {
                CLIENTS.remove(ws);
                System.err.println("WebSocket error on client: " + error);
            });
        });

        int port = 8082;
        server.listen(port, "0.0.0.0");
        System.out.printf("WebSocket Chat Server listening on ws://localhost:%d/chat%n", port);
        server.run();
    }

    private static void broadcast(String text) {
        // Broadcast in parallel across Project Loom Virtual Threads
        for (WebSocket client : CLIENTS) {
            Thread.startVirtualThread(() -> {
                try {
                    if (client.isOpen()) {
                        client.sendText(text);
                    }
                } catch (Throwable ignored) {
                    CLIENTS.remove(client);
                }
            });
        }
    }
}

