package com.teamzemo.aegon.middleware;

import com.teamzemo.aegon.http.Context;

/**
 * Invocation handle for continuing the onion-style middleware chain.
 */
@FunctionalInterface
public interface Next {

    /**
     * Advances execution to the next middleware or terminal route handler in the pipeline.
     *
     * @param ctx current HTTP exchange context
     * @throws Exception any exception propagated from downstream handlers
     */
    void call(Context ctx) throws Exception;
}

