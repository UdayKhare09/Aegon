package com.teamzemo.aegon.sse;

import com.teamzemo.aegon.http.Context;

/**
 * Functional interface for Server-Sent Events (SSE) streaming endpoints.
 *
 * <p>Executed automatically inside a dedicated Java Virtual Thread to support
 * straightforward blocking iteration and sleep loops.
 */
@FunctionalInterface
public interface SseHandler {

    /**
     * Executes the SSE streaming session.
     *
     * @param ctx initial HTTP exchange context
     * @param sse live SSE write stream
     * @throws Exception any unhandled error
     */
    void handle(Context ctx, SseStream sse) throws Exception;
}

