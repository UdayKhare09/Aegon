package com.teamzemo.aegon.middleware;

import com.teamzemo.aegon.http.Context;

/**
 * Onion-style HTTP middleware function matching Aegon C++ {@code aegon::http::Middleware}.
 */
@FunctionalInterface
public interface Middleware {

    /**
     * Intercepts and processes an HTTP exchange.
     *
     * <p>Implementations should invoke {@code next.call(ctx)} to continue downstream processing,
     * or omit it to short-circuit the request and return the response immediately.
     *
     * @param ctx current exchange context
     * @param next continuation handle to downstream pipeline
     * @throws Exception any exception during processing
     */
    void handle(Context ctx, Next next) throws Exception;
}

