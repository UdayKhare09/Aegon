package com.teamzemo.aegon.ws;

import java.util.function.BiConsumer;
import java.util.function.Consumer;

/**
 * Configuration builder for registering WebSocket lifecycle callbacks.
 */
public final class WebSocketSession {

    @FunctionalInterface
    public interface CloseHandler {
        void handle(WebSocket ws, int code, String reason);
    }

    private Consumer<WebSocket> openHandler;
    private BiConsumer<WebSocket, String> textHandler;
    private BiConsumer<WebSocket, byte[]> binaryHandler;
    private CloseHandler closeHandler;
    private BiConsumer<WebSocket, String> errorHandler;

    public WebSocketSession() {}

    public WebSocketSession onOpen(Consumer<WebSocket> handler) {
        this.openHandler = handler;
        return this;
    }

    public WebSocketSession onText(BiConsumer<WebSocket, String> handler) {
        this.textHandler = handler;
        return this;
    }

    public WebSocketSession onBinary(BiConsumer<WebSocket, byte[]> handler) {
        this.binaryHandler = handler;
        return this;
    }

    public WebSocketSession onClose(CloseHandler handler) {
        this.closeHandler = handler;
        return this;
    }

    public WebSocketSession onError(BiConsumer<WebSocket, String> handler) {
        this.errorHandler = handler;
        return this;
    }

    public Consumer<WebSocket> getOpenHandler() { return openHandler; }
    public BiConsumer<WebSocket, String> getTextHandler() { return textHandler; }
    public BiConsumer<WebSocket, byte[]> getBinaryHandler() { return binaryHandler; }
    public CloseHandler getCloseHandler() { return closeHandler; }
    public BiConsumer<WebSocket, String> getErrorHandler() { return errorHandler; }
}

