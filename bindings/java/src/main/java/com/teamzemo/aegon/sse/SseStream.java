package com.teamzemo.aegon.sse;

import com.teamzemo.aegon.internal.ffi.FfiSupport;
import com.teamzemo.aegon.internal.ffi.SseFfi;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Objects;

/**
 * Live Server-Sent Events (SSE) write channel matching Aegon C++ {@code aegon::http::sse::SseWriter}.
 *
 * <p>Thread-safe; sends may be called from any Java Virtual Thread.
 */
public final class SseStream implements AutoCloseable {

    private final MemorySegment ssePtr;
    private final java.util.concurrent.atomic.AtomicBoolean closed = new java.util.concurrent.atomic.AtomicBoolean(false);

    public SseStream(MemorySegment ssePtr) {
        this.ssePtr = Objects.requireNonNull(ssePtr, "ssePtr cannot be null");
    }

    public MemorySegment handle() {
        return ssePtr;
    }

    /**
     * Sends a named SSE event with payload (e.g. {@code event: myEvent\ndata: ...\n\n}).
     *
     * @param name event type name
     * @param data payload string
     */
    public void sendEvent(String name, String data) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nameSeg = FfiSupport.makeStr(arena, name);
            MemorySegment dataSeg = FfiSupport.makeStr(arena, data);
            int err = (int) SseFfi.MH_aegon_sse_send_event.invokeExact(ssePtr, nameSeg, dataSeg);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to send SSE event: " + t.getMessage(), t);
        }
    }

    /**
     * Sends an unnamed SSE data message (e.g. {@code data: ...\n\n}).
     *
     * @param data payload string
     */
    public void sendData(String data) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment dataSeg = FfiSupport.makeStr(arena, data);
            int err = (int) SseFfi.MH_aegon_sse_send_data.invokeExact(ssePtr, dataSeg);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to send SSE data: " + t.getMessage(), t);
        }
    }

    /**
     * Sends an SSE comment (heartbeat / keep-alive, e.g. {@code : heartbeat\n\n}).
     *
     * @param text comment text
     */
    public void sendComment(String text) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment textSeg = FfiSupport.makeStr(arena, text);
            int err = (int) SseFfi.MH_aegon_sse_send_comment.invokeExact(ssePtr, textSeg);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to send SSE comment: " + t.getMessage(), t);
        }
    }

    /**
     * Sends a retry reconnect interval directive to the client (e.g. {@code retry: 3000\n\n}).
     *
     * @param milliseconds reconnect interval in milliseconds
     */
    public void retry(int milliseconds) {
        try {
            int err = (int) SseFfi.MH_aegon_sse_send_retry.invokeExact(ssePtr, milliseconds);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to send SSE retry: " + t.getMessage(), t);
        }
    }

    /**
     * Checks if the SSE connection is still open and active.
     *
     * @return true if open, false if client disconnected
     */
    public boolean isOpen() {
        if (closed.get()) return false;
        try {
            return (boolean) SseFfi.MH_aegon_sse_is_open.invokeExact(ssePtr);
        } catch (Throwable t) {
            return false;
        }
    }

    /**
     * Closes the SSE connection and sends the terminal zero-chunk.
     */
    @Override
    public void close() {
        if (closed.compareAndSet(false, true)) {
            try {
                SseFfi.MH_aegon_sse_close.invokeExact(ssePtr);
                SseFfi.MH_aegon_sse_destroy.invokeExact(ssePtr);
            } catch (Throwable ignored) {}
        }
    }
}
