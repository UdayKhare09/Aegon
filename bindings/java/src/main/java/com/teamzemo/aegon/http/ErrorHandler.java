package com.teamzemo.aegon.http;

/**
 * Functional interface for custom global error handling matching Aegon C++ {@code Router::ErrorHandler}.
 */
@FunctionalInterface
public interface ErrorHandler {

    /**
     * Intercepts unhandled errors thrown during request processing.
     *
     * @param ctx unified HTTP exchange context
     * @param exceptionMessage error or exception message
     * @throws Exception any exception during error handling
     */
    void handleError(Context ctx, String exceptionMessage) throws Exception;
}

