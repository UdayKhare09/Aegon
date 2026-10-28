package com.teamzemo.aegon.http;

import com.teamzemo.aegon.internal.ffi.*;
import com.teamzemo.aegon.middleware.*;
import com.teamzemo.aegon.ws.WebSocketSession;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.Objects;
import java.util.function.Consumer;

/**
 * Route group scoped to a common URL path prefix matching Aegon C++ {@code aegon::http::RouteGroup}.
 *
 * <p>Supports hierarchical nested sub-groups, group-scoped middleware, static files, and WebSocket endpoints.
 */
public final class RouteGroup {

    private final MemorySegment groupPtr;
    private final String prefix;

    public RouteGroup(MemorySegment groupPtr, String prefix) {
        this.groupPtr = Objects.requireNonNull(groupPtr, "groupPtr cannot be null");
        this.prefix = prefix != null ? prefix : "";
    }

    /**
     * Returns the native pointer to this {@code aegon_group_t}.
     *
     * @return memory segment pointer
     */
    public MemorySegment handle() {
        return groupPtr;
    }

    /**
     * URL path prefix of this route group (e.g. "/api/v1").
     *
     * @return prefix string
     */
    public String prefix() {
        return prefix;
    }

    /**
     * Creates a nested sub-group under this group's prefix.
     *
     * @param subPrefix path sub-prefix (e.g. "/users")
     * @return nested {@link RouteGroup}
     */
    public RouteGroup group(String subPrefix) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment prefixSeg = arena.allocateFrom(subPrefix);
            MemorySegment childPtr = (MemorySegment) RouterFfi.MH_aegon_group_group.invokeExact(groupPtr, prefixSeg);
            if (childPtr.equals(MemorySegment.NULL)) {
                throw new RuntimeException("Failed to create nested group: " + subPrefix);
            }
            return new RouteGroup(childPtr, this.prefix + subPrefix);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to create nested RouteGroup: " + t.getMessage(), t);
        }
    }

    /**
     * Creates and configures a nested hierarchical route group with a lambda configurer.
     *
     * @param subPrefix nested URL prefix
     * @param configurer configuration callback receiving the nested {@link RouteGroup}
     * @return this {@link RouteGroup} for chaining
     */
    public RouteGroup group(String subPrefix, java.util.function.Consumer<RouteGroup> configurer) {
        RouteGroup g = group(subPrefix);
        configurer.accept(g);
        return this;
    }

    /* -------------------------------------------------------------------------
     * Synchronous Routes (Reactor Fast-Path)
     * ------------------------------------------------------------------------- */

    public RouteGroup get(String pattern, Handler handler) {
        return add(Method.GET, pattern, handler);
    }

    public RouteGroup post(String pattern, Handler handler) {
        return add(Method.POST, pattern, handler);
    }

    public RouteGroup put(String pattern, Handler handler) {
        return add(Method.PUT, pattern, handler);
    }

    public RouteGroup del(String pattern, Handler handler) {
        return add(Method.DELETE, pattern, handler);
    }

    public RouteGroup delete(String pattern, Handler handler) {
        return del(pattern, handler);
    }

    public RouteGroup patch(String pattern, Handler handler) {
        return add(Method.PATCH, pattern, handler);
    }

    public RouteGroup all(String pattern, Handler handler) {
        return add(Method.UNKNOWN, pattern, handler);
    }

    public RouteGroup add(Method method, String pattern, Handler handler) {
        long id = Router.registerSyncHandler(handler);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment patternSeg = arena.allocateFrom(pattern);
            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) RouterFfi.MH_aegon_group_add.invokeExact(
                    groupPtr,
                    method.nativeCode(),
                    patternSeg,
                    Router.SYNC_UPCALL_STUB,
                    userdata,
                    Router.CLEANUP_UPCALL_STUB
            );
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            Router.unregisterSyncHandler(id);
            throw new RuntimeException("Failed to register group route: " + t.getMessage(), t);
        }
    }

    /* -------------------------------------------------------------------------
     * Asynchronous Routes (Automated VirtualThread Dispatch)
     * ------------------------------------------------------------------------- */

    public RouteGroup getAsync(String pattern, Handler handler) {
        return addAsync(Method.GET, pattern, handler);
    }

    public RouteGroup postAsync(String pattern, Handler handler) {
        return addAsync(Method.POST, pattern, handler);
    }

    public RouteGroup putAsync(String pattern, Handler handler) {
        return addAsync(Method.PUT, pattern, handler);
    }

    public RouteGroup delAsync(String pattern, Handler handler) {
        return addAsync(Method.DELETE, pattern, handler);
    }

    public RouteGroup deleteAsync(String pattern, Handler handler) {
        return delAsync(pattern, handler);
    }

    public RouteGroup patchAsync(String pattern, Handler handler) {
        return addAsync(Method.PATCH, pattern, handler);
    }

    public RouteGroup allAsync(String pattern, Handler handler) {
        return addAsync(Method.UNKNOWN, pattern, handler);
    }

    public RouteGroup addAsync(Method method, String pattern, Handler handler) {
        long id = Router.registerAsyncHandler(handler);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment patternSeg = arena.allocateFrom(pattern);
            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) RouterFfi.MH_aegon_group_add_async.invokeExact(
                    groupPtr,
                    method.nativeCode(),
                    patternSeg,
                    Router.ASYNC_UPCALL_STUB,
                    userdata,
                    Router.CLEANUP_UPCALL_STUB
            );
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            Router.unregisterAsyncHandler(id);
            throw new RuntimeException("Failed to register async group route: " + t.getMessage(), t);
        }
    }

    /**
     * Registers a route in this group with route-scoped middleware slots matching {@code aegon_group_add_mw}.
     *
     * @param method HTTP method
     * @param pattern URL route pattern
     * @param middlewares list of middleware to apply before handler
     * @param handler terminal route handler
     * @return this {@link RouteGroup}
     */
    public RouteGroup addWithMiddleware(Method method, String pattern, java.util.List<com.teamzemo.aegon.middleware.Middleware> middlewares, Handler handler) {
        long handlerId = Router.registerSyncHandler(handler);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment patternSeg = arena.allocateFrom(pattern);
            int mwCount = middlewares != null ? middlewares.size() : 0;
            MemorySegment slots = mwCount > 0 ? arena.allocate(com.teamzemo.aegon.internal.ffi.MiddlewareFfi.MIDDLEWARE_SLOT_LAYOUT, mwCount) : MemorySegment.NULL;
            for (int i = 0; i < mwCount; i++) {
                long mwId = Router.registerMiddleware(middlewares.get(i));
                MemorySegment slot = slots.asSlice(i * com.teamzemo.aegon.internal.ffi.MiddlewareFfi.MIDDLEWARE_SLOT_LAYOUT.byteSize(), com.teamzemo.aegon.internal.ffi.MiddlewareFfi.MIDDLEWARE_SLOT_LAYOUT);
                slot.set(java.lang.foreign.ValueLayout.ADDRESS, 0, Router.MIDDLEWARE_UPCALL_STUB);
                slot.set(java.lang.foreign.ValueLayout.ADDRESS, java.lang.foreign.ValueLayout.ADDRESS.byteSize(), MemorySegment.ofAddress(mwId));
                slot.set(java.lang.foreign.ValueLayout.ADDRESS, java.lang.foreign.ValueLayout.ADDRESS.byteSize() * 2, Router.MIDDLEWARE_CLEANUP_STUB);
            }
            MemorySegment handlerUserdata = MemorySegment.ofAddress(handlerId);
            int err = (int) RouterFfi.MH_aegon_group_add_mw.invokeExact(
                    groupPtr,
                    method.nativeCode(),
                    patternSeg,
                    slots,
                    (long) mwCount,
                    Router.SYNC_UPCALL_STUB,
                    handlerUserdata,
                    Router.CLEANUP_UPCALL_STUB
            );
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            Router.unregisterSyncHandler(handlerId);
            throw new RuntimeException("Failed to add group route with middleware: " + t.getMessage(), t);
        }
    }

    /* -------------------------------------------------------------------------
     * Middleware & Extensions
     * ------------------------------------------------------------------------- */

    public RouteGroup use(Middleware middleware) {
        long id = Router.registerMiddleware(middleware);
        try {
            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) MiddlewareFfi.MH_aegon_group_use.invokeExact(
                    groupPtr,
                    Router.MIDDLEWARE_UPCALL_STUB,
                    userdata,
                    Router.MIDDLEWARE_CLEANUP_STUB
            );
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            Router.unregisterMiddleware(id);
            throw new RuntimeException("Failed to register group middleware: " + t.getMessage(), t);
        }
    }

    public RouteGroup useCors(CorsConfig config) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment cfgSeg = arena.allocate(128);
            ExtensionsFfi.MH_aegon_cors_config_init.invokeExact(cfgSeg);
            int err = (int) ExtensionsFfi.MH_aegon_group_use_cors.invokeExact(groupPtr, cfgSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to apply group CORS: " + t.getMessage(), t);
        }
    }

    public RouteGroup useCompress(CompressConfig config) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment cfgSeg = arena.allocate(64);
            ExtensionsFfi.MH_aegon_compress_config_init.invokeExact(cfgSeg);
            int err = (int) ExtensionsFfi.MH_aegon_group_use_compress.invokeExact(groupPtr, cfgSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to apply group compression: " + t.getMessage(), t);
        }
    }

    public RouteGroup useSecurityHeaders(SecurityHeadersConfig config) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment cfgSeg = arena.allocate(128);
            ExtensionsFfi.MH_aegon_security_config_init.invokeExact(cfgSeg);
            int err = (int) ExtensionsFfi.MH_aegon_group_use_security_headers.invokeExact(groupPtr, cfgSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to apply group security headers: " + t.getMessage(), t);
        }
    }

    public RouteGroup useAccessLog(AccessLogConfig config) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment cfgSeg = arena.allocate(128);
            ExtensionsFfi.MH_aegon_access_log_config_init.invokeExact(cfgSeg);
            int err = (int) ExtensionsFfi.MH_aegon_group_use_access_log.invokeExact(groupPtr, cfgSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to apply group access log: " + t.getMessage(), t);
        }
    }

    public RouteGroup staticFiles(String urlPrefix, String directory) {
        return staticFiles(urlPrefix, directory, StaticFilesConfig.defaults());
    }

    public RouteGroup staticFiles(String urlPrefix, String directory, StaticFilesConfig config) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment preSeg = arena.allocateFrom(urlPrefix);
            MemorySegment dirSeg = arena.allocateFrom(directory);
            MemorySegment cfgSeg = arena.allocate(128);
            ExtensionsFfi.MH_aegon_static_files_config_init.invokeExact(cfgSeg);
            int err = (int) ExtensionsFfi.MH_aegon_group_static_files.invokeExact(groupPtr, preSeg, dirSeg, cfgSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to register group static files: " + t.getMessage(), t);
        }
    }

    public RouteGroup ws(String pattern, Consumer<WebSocketSession> configurer) {
        WebSocketSession session = new WebSocketSession();
        configurer.accept(session);
        Router.registerGroupWebSocket(groupPtr, pattern, session);
        return this;
    }
}
