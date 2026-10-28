package com.teamzemo.aegon.http;

/**
 * Functional interface for HTTP route handlers matching Aegon C++ {@code void(http::Context&)}.
 */
@FunctionalInterface
public interface Handler {

    /**
     * Handles an incoming HTTP request within the current exchange context.
     *
     * @param ctx unified HTTP exchange context encapsulating {@code ctx.req()} and {@code ctx.res()}
     * @throws Exception any exception thrown will be intercepted and mapped to an RFC 7807 500 error
     */
    void handle(Context ctx) throws Exception;
}

