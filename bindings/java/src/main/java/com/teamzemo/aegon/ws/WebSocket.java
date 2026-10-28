package com.teamzemo.aegon.ws;

import com.teamzemo.aegon.internal.ffi.FfiSupport;
import com.teamzemo.aegon.internal.ffi.WebSocketFfi;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Objects;

/**
 * Handle to an active RFC 6455 WebSocket connection.
 */
public final class WebSocket {

    private final MemorySegment wsPtr;

    public WebSocket(MemorySegment wsPtr) {
        this.wsPtr = Objects.requireNonNull(wsPtr, "wsPtr cannot be null");
    }

    public MemorySegment handle() {
        return wsPtr;
    }

    /**
     * Sends a UTF-8 text frame to the connected client.
     *
     * @param text text payload
     */
    public void sendText(String text) {
        Arena arena = Arena.ofAuto();
        MemorySegment strSeg = FfiSupport.makeStr(arena, text);
        try {
            int err = (int) WebSocketFfi.MH_aegon_ws_send_text.invokeExact(wsPtr, strSeg);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to send WebSocket text: " + t.getMessage(), t);
        }
    }

    /**
     * Sends a binary frame to the connected client.
     *
     * @param data binary payload
     */
    public void sendBinary(byte[] data) {
        Arena arena = Arena.ofAuto();
        MemorySegment strSeg = FfiSupport.makeStr(arena, data);
        try {
            int err = (int) WebSocketFfi.MH_aegon_ws_send_binary.invokeExact(wsPtr, strSeg);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to send WebSocket binary: " + t.getMessage(), t);
        }
    }

    /**
     * Sends a Ping control frame with payload.
     *
     * @param payload ping payload
     */
    public void sendPing(byte[] payload) {
        Arena arena = Arena.ofAuto();
        MemorySegment strSeg = FfiSupport.makeStr(arena, payload);
        try {
            int err = (int) WebSocketFfi.MH_aegon_ws_send_ping.invokeExact(wsPtr, strSeg);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to send WebSocket ping: " + t.getMessage(), t);
        }
    }

    /**
     * Sends a Pong control frame with payload.
     *
     * @param payload pong payload
     */
    public void sendPong(byte[] payload) {
        Arena arena = Arena.ofAuto();
        MemorySegment strSeg = FfiSupport.makeStr(arena, payload);
        try {
            int err = (int) WebSocketFfi.MH_aegon_ws_send_pong.invokeExact(wsPtr, strSeg);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to send WebSocket pong: " + t.getMessage(), t);
        }
    }

    /**
     * Initiates standard WebSocket close handshake.
     *
     * @param code status code (e.g. 1000 for normal closure)
     * @param reason optional reason string
     */
    public void close(int code, String reason) {
        Arena arena = Arena.ofAuto();
        MemorySegment strSeg = FfiSupport.makeStr(arena, reason != null ? reason : "");
        try {
            int err = (int) WebSocketFfi.MH_aegon_ws_close.invokeExact(wsPtr, (short) code, strSeg);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to close WebSocket: " + t.getMessage(), t);
        }
    }

    /**
     * Initiates normal closure (code 1000).
     */
    public void close() {
        close(WsCloseCode.NORMAL.code(), "");
    }

    /**
     * Checks if the WebSocket connection is currently open.
     *
     * @return true if open, false otherwise
     */
    public boolean isOpen() {
        try {
            return (boolean) WebSocketFfi.MH_aegon_ws_is_open.invokeExact(wsPtr);
        } catch (Throwable t) {
            return false;
        }
    }
}
