package com.teamzemo.aegon.http;

import com.teamzemo.aegon.internal.ffi.*;
import com.teamzemo.aegon.middleware.*;
import com.teamzemo.aegon.sse.SseHandler;
import com.teamzemo.aegon.sse.SseStream;
import com.teamzemo.aegon.ws.WebSocket;
import com.teamzemo.aegon.ws.WebSocketSession;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;

/**
 * Two-tier high-performance HTTP Router matching Aegon C++ {@code aegon::http::Router}.
 *
 * <p>Supports:
 * <ul>
 *   <li>Synchronous fast-path routes ({@code get}, {@code post}, {@code del}, etc.) running inline on native reactor threads with 0 coroutines.</li>
 *   <li>Automated Project Loom VirtualThread routes ({@code getAsync}, {@code postAsync}, {@code delAsync}, etc.) for zero-boilerplate blocking I/O.</li>
 *   <li>Hierarchical prefix route groups via {@link #group(String)}.</li>
 *   <li>Built-in middleware (CORS, Compression, Security Headers, Access Logging, Static Files).</li>
 *   <li>RFC 6455 WebSockets and Server-Sent Events (SSE).</li>
 * </ul>
 */
public final class Router implements AutoCloseable {

    private static final AtomicLong ID_SEQ = new AtomicLong(1);
    private static final ConcurrentHashMap<Long, Handler> SYNC_REGISTRY = new ConcurrentHashMap<>();
    private static final ConcurrentHashMap<Long, Handler> ASYNC_REGISTRY = new ConcurrentHashMap<>();
    private static final ConcurrentHashMap<Long, ErrorHandler> ERROR_REGISTRY = new ConcurrentHashMap<>();
    private static volatile ErrorHandler globalErrorHandler;
    private static final ConcurrentHashMap<Long, Middleware> MIDDLEWARE_REGISTRY = new ConcurrentHashMap<>();
    private static final ConcurrentHashMap<Long, WebSocketSession> WS_SESSION_REGISTRY = new ConcurrentHashMap<>();
    private static final ConcurrentHashMap<Long, SseHandler> SSE_REGISTRY = new ConcurrentHashMap<>();

    /* Shared Native Upcall Stubs (allocated once globally) */
    public static final MemorySegment SYNC_UPCALL_STUB;
    public static final MemorySegment ASYNC_UPCALL_STUB;
    public static final MemorySegment SSE_UPCALL_STUB;
    public static final MemorySegment CLEANUP_UPCALL_STUB;
    public static final MemorySegment ERROR_UPCALL_STUB;
    public static final MemorySegment MIDDLEWARE_UPCALL_STUB;
    public static final MemorySegment MIDDLEWARE_CLEANUP_STUB;

    public static final MemorySegment WS_ON_OPEN_STUB;
    public static final MemorySegment WS_ON_MESSAGE_STUB;
    public static final MemorySegment WS_ON_CLOSE_STUB;
    public static final MemorySegment WS_ON_ERROR_STUB;

    static {
        try {
            MethodHandles.Lookup lookup = MethodHandles.lookup();

            MethodHandle mhSync = lookup.findStatic(Router.class, "syncTrampoline",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class));
            SYNC_UPCALL_STUB = FfiSupport.LINKER.upcallStub(mhSync, RouterFfi.HANDLER_UPCALL_DESC, Arena.global());

            MethodHandle mhAsync = lookup.findStatic(Router.class, "asyncTrampoline",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class));
            ASYNC_UPCALL_STUB = FfiSupport.LINKER.upcallStub(mhAsync, RouterFfi.HANDLER_UPCALL_DESC, Arena.global());

            MethodHandle mhSse = lookup.findStatic(Router.class, "sseTrampoline",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class));
            SSE_UPCALL_STUB = FfiSupport.LINKER.upcallStub(mhSse, RouterFfi.HANDLER_UPCALL_DESC, Arena.global());

            MethodHandle mhCleanup = lookup.findStatic(Router.class, "cleanupTrampoline",
                    MethodType.methodType(void.class, MemorySegment.class));
            CLEANUP_UPCALL_STUB = FfiSupport.LINKER.upcallStub(mhCleanup, RouterFfi.CLEANUP_UPCALL_DESC, Arena.global());

            MethodHandle mhErr = lookup.findStatic(Router.class, "errorTrampoline",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class, MemorySegment.class));
            ERROR_UPCALL_STUB = FfiSupport.LINKER.upcallStub(mhErr, RouterFfi.ERROR_HANDLER_UPCALL_DESC, Arena.global());

            MethodHandle mhMw = lookup.findStatic(Router.class, "middlewareTrampoline",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class, MemorySegment.class));
            MIDDLEWARE_UPCALL_STUB = FfiSupport.LINKER.upcallStub(mhMw, MiddlewareFfi.MIDDLEWARE_UPCALL_DESC, Arena.global());

            MethodHandle mhMwCleanup = lookup.findStatic(Router.class, "middlewareCleanupTrampoline",
                    MethodType.methodType(void.class, MemorySegment.class));
            MIDDLEWARE_CLEANUP_STUB = FfiSupport.LINKER.upcallStub(mhMwCleanup, RouterFfi.CLEANUP_UPCALL_DESC, Arena.global());

            MethodHandle mhWsOpen = lookup.findStatic(Router.class, "wsOnOpenTrampoline",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class, MemorySegment.class));
            WS_ON_OPEN_STUB = FfiSupport.LINKER.upcallStub(mhWsOpen, WebSocketFfi.WS_ON_OPEN_DESC, Arena.global());

            MethodHandle mhWsMsg = lookup.findStatic(Router.class, "wsOnMessageTrampoline",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class, int.class, MemorySegment.class));
            WS_ON_MESSAGE_STUB = FfiSupport.LINKER.upcallStub(mhWsMsg, WebSocketFfi.WS_ON_MESSAGE_DESC, Arena.global());

            MethodHandle mhWsClose = lookup.findStatic(Router.class, "wsOnCloseTrampoline",
                    MethodType.methodType(void.class, MemorySegment.class, short.class, MemorySegment.class, MemorySegment.class));
            WS_ON_CLOSE_STUB = FfiSupport.LINKER.upcallStub(mhWsClose, WebSocketFfi.WS_ON_CLOSE_DESC, Arena.global());

            MethodHandle mhWsErr = lookup.findStatic(Router.class, "wsOnErrorTrampoline",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class, MemorySegment.class));
            WS_ON_ERROR_STUB = FfiSupport.LINKER.upcallStub(mhWsErr, WebSocketFfi.WS_ON_ERROR_DESC, Arena.global());
        } catch (Throwable t) {
            throw new ExceptionInInitializerError("Failed to initialize Aegon FFM upcall stubs: " + t.getMessage());
        }
    }

    private MemorySegment routerPtr;

    public Router() {
        try {
            this.routerPtr = (MemorySegment) RouterFfi.MH_aegon_router_create.invokeExact();
            if (routerPtr.equals(MemorySegment.NULL)) {
                throw new RuntimeException("Failed to allocate native Aegon router");
            }
        } catch (Throwable t) {
            throw new RuntimeException("Failed to create Router: " + t.getMessage(), t);
        }
    }

    public Router(MemorySegment routerPtr) {
        this.routerPtr = Objects.requireNonNull(routerPtr, "routerPtr cannot be null");
    }

    /**
     * Factory method creating a fresh, unattached {@link Router}.
     *
     * @return new {@link Router}
     */
    public static Router create() {
        return new Router();
    }

    /**
     * Returns the native pointer to this {@code aegon_router_t}.
     *
     * @return memory segment pointer
     */
    public MemorySegment handle() {
        return routerPtr;
    }

    /* -------------------------------------------------------------------------
     * Synchronous Routes (Reactor Fast-Path: 0 coroutines, 0 thread-switch)
     * ------------------------------------------------------------------------- */

    public Router get(String pattern, Handler handler) {
        return add(Method.GET, pattern, handler);
    }

    public Router post(String pattern, Handler handler) {
        return add(Method.POST, pattern, handler);
    }

    public Router put(String pattern, Handler handler) {
        return add(Method.PUT, pattern, handler);
    }

    public Router del(String pattern, Handler handler) {
        return add(Method.DELETE, pattern, handler);
    }

    public Router delete(String pattern, Handler handler) {
        return del(pattern, handler);
    }

    public Router patch(String pattern, Handler handler) {
        return add(Method.PATCH, pattern, handler);
    }

    public Router all(String pattern, Handler handler) {
        return add(Method.UNKNOWN, pattern, handler);
    }

    public Router add(Method method, String pattern, Handler handler) {
        checkNotClosed();
        long id = registerSyncHandler(handler);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment patternSeg = arena.allocateFrom(pattern);
            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) RouterFfi.MH_aegon_router_add.invokeExact(
                    routerPtr,
                    method.nativeCode(),
                    patternSeg,
                    SYNC_UPCALL_STUB,
                    userdata,
                    CLEANUP_UPCALL_STUB
            );
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            unregisterSyncHandler(id);
            throw new RuntimeException("Failed to register sync route: " + t.getMessage(), t);
        }
    }

    /* -------------------------------------------------------------------------
     * Asynchronous Routes (Automated Project Loom VirtualThread Dispatch)
     * ------------------------------------------------------------------------- */

    public Router getAsync(String pattern, Handler handler) {
        return addAsync(Method.GET, pattern, handler);
    }

    public Router postAsync(String pattern, Handler handler) {
        return addAsync(Method.POST, pattern, handler);
    }

    public Router putAsync(String pattern, Handler handler) {
        return addAsync(Method.PUT, pattern, handler);
    }

    public Router delAsync(String pattern, Handler handler) {
        return addAsync(Method.DELETE, pattern, handler);
    }

    public Router deleteAsync(String pattern, Handler handler) {
        return delAsync(pattern, handler);
    }

    public Router patchAsync(String pattern, Handler handler) {
        return addAsync(Method.PATCH, pattern, handler);
    }

    public Router allAsync(String pattern, Handler handler) {
        return addAsync(Method.UNKNOWN, pattern, handler);
    }

    public Router addAsync(Method method, String pattern, Handler handler) {
        checkNotClosed();
        long id = registerAsyncHandler(handler);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment patternSeg = arena.allocateFrom(pattern);
            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) RouterFfi.MH_aegon_router_add_async.invokeExact(
                    routerPtr,
                    method.nativeCode(),
                    patternSeg,
                    ASYNC_UPCALL_STUB,
                    userdata,
                    CLEANUP_UPCALL_STUB
            );
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            unregisterAsyncHandler(id);
            throw new RuntimeException("Failed to register async route: " + t.getMessage(), t);
        }
    }

    /* -------------------------------------------------------------------------
     * Route Groups
     * ------------------------------------------------------------------------- */

    /**
     * Mounts a new prefix-scoped route group matching Aegon C++ {@code router.group("/prefix")}.
     *
     * @param prefix URL path prefix (e.g. "/api/v1")
     * @return new {@link RouteGroup}
     */
    public RouteGroup group(String prefix) {
        checkNotClosed();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment prefixSeg = arena.allocateFrom(prefix);
            MemorySegment groupPtr = (MemorySegment) RouterFfi.MH_aegon_router_group.invokeExact(routerPtr, prefixSeg);
            if (groupPtr.equals(MemorySegment.NULL)) {
                throw new RuntimeException("Failed to create route group: " + prefix);
            }
            return new RouteGroup(groupPtr, prefix);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to create RouteGroup: " + t.getMessage(), t);
        }
    }

    /**
     * Creates and configures a hierarchical prefix route group with a lambda configurer.
     *
     * @param prefix URL prefix (e.g. {@code "/api/v1"})
     * @param configurer configuration callback receiving the {@link RouteGroup}
     * @return this {@link Router} for chaining
     */
    public Router group(String prefix, Consumer<RouteGroup> configurer) {
        RouteGroup g = group(prefix);
        configurer.accept(g);
        return this;
    }

    /* -------------------------------------------------------------------------
     * Fallback Handlers
     * ------------------------------------------------------------------------- */

    public Router onError(ErrorHandler handler) {
        checkNotClosed();
        globalErrorHandler = handler;
        long id = ID_SEQ.getAndIncrement();
        ERROR_REGISTRY.put(id, handler);
        try {
            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) RouterFfi.MH_aegon_router_on_error.invokeExact(
                    routerPtr,
                    ERROR_UPCALL_STUB,
                    userdata,
                    CLEANUP_UPCALL_STUB
            );
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            ERROR_REGISTRY.remove(id);
            throw new RuntimeException("Failed to register error handler: " + t.getMessage(), t);
        }
    }

    public Router onNotFound(Handler handler) {
        checkNotClosed();
        long id = registerSyncHandler(handler);
        try {
            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) RouterFfi.MH_aegon_router_on_not_found.invokeExact(
                    routerPtr,
                    SYNC_UPCALL_STUB,
                    userdata,
                    CLEANUP_UPCALL_STUB
            );
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            unregisterSyncHandler(id);
            throw new RuntimeException("Failed to register on_not_found handler: " + t.getMessage(), t);
        }
    }

    public Router onMethodNotAllowed(Handler handler) {
        checkNotClosed();
        long id = registerSyncHandler(handler);
        try {
            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) RouterFfi.MH_aegon_router_on_method_not_allowed.invokeExact(
                    routerPtr,
                    SYNC_UPCALL_STUB,
                    userdata,
                    CLEANUP_UPCALL_STUB
            );
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            unregisterSyncHandler(id);
            throw new RuntimeException("Failed to register on_method_not_allowed handler: " + t.getMessage(), t);
        }
    }

    /* -------------------------------------------------------------------------
     * Middleware & Built-in Extensions
     * ------------------------------------------------------------------------- */

    public Router use(Middleware middleware) {
        checkNotClosed();
        long id = registerMiddleware(middleware);
        try {
            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) MiddlewareFfi.MH_aegon_router_use.invokeExact(
                    routerPtr,
                    MIDDLEWARE_UPCALL_STUB,
                    userdata,
                    MIDDLEWARE_CLEANUP_STUB
            );
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            unregisterMiddleware(id);
            throw new RuntimeException("Failed to register middleware: " + t.getMessage(), t);
        }
    }

    public Router useCors(CorsConfig config) {
        checkNotClosed();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment cfgSeg = arena.allocate(128);
            ExtensionsFfi.MH_aegon_cors_config_init.invokeExact(cfgSeg);
            int err = (int) ExtensionsFfi.MH_aegon_router_use_cors.invokeExact(routerPtr, cfgSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to apply CORS: " + t.getMessage(), t);
        }
    }

    public Router useCompress(CompressConfig config) {
        checkNotClosed();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment cfgSeg = arena.allocate(64);
            ExtensionsFfi.MH_aegon_compress_config_init.invokeExact(cfgSeg);
            int err = (int) ExtensionsFfi.MH_aegon_router_use_compress.invokeExact(routerPtr, cfgSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to apply compression: " + t.getMessage(), t);
        }
    }

    public Router useSecurityHeaders(SecurityHeadersConfig config) {
        checkNotClosed();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment cfgSeg = arena.allocate(128);
            ExtensionsFfi.MH_aegon_security_config_init.invokeExact(cfgSeg);
            int err = (int) ExtensionsFfi.MH_aegon_router_use_security_headers.invokeExact(routerPtr, cfgSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to apply security headers: " + t.getMessage(), t);
        }
    }

    public Router useAccessLog(AccessLogConfig config) {
        checkNotClosed();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment cfgSeg = arena.allocate(128);
            ExtensionsFfi.MH_aegon_access_log_config_init.invokeExact(cfgSeg);
            int err = (int) ExtensionsFfi.MH_aegon_router_use_access_log.invokeExact(routerPtr, cfgSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to apply access log: " + t.getMessage(), t);
        }
    }

    public Router staticFiles(String urlPrefix, String directory) {
        return staticFiles(urlPrefix, directory, StaticFilesConfig.defaults());
    }

    public Router staticFiles(String urlPrefix, String directory, StaticFilesConfig config) {
        checkNotClosed();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment preSeg = arena.allocateFrom(urlPrefix);
            MemorySegment dirSeg = arena.allocateFrom(directory);
            MemorySegment cfgSeg = arena.allocate(128);
            ExtensionsFfi.MH_aegon_static_files_config_init.invokeExact(cfgSeg);
            int err = (int) ExtensionsFfi.MH_aegon_router_static_files.invokeExact(routerPtr, preSeg, dirSeg, cfgSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to register static files: " + t.getMessage(), t);
        }
    }

    /* -------------------------------------------------------------------------
     * Real-Time (WebSockets & SSE)
     * ------------------------------------------------------------------------- */

    public Router ws(String pattern, Consumer<WebSocketSession> configurer) {
        checkNotClosed();
        WebSocketSession session = new WebSocketSession();
        configurer.accept(session);
        registerRouterWebSocket(routerPtr, pattern, session);
        return this;
    }

    public Router wsEcho(String pattern) {
        checkNotClosed();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment patSeg = arena.allocateFrom(pattern);
            int err = (int) WebSocketFfi.MH_aegon_router_ws_echo.invokeExact(routerPtr, patSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to register WebSocket echo: " + t.getMessage(), t);
        }
    }

    public Router sse(String pattern, SseHandler handler) {
        checkNotClosed();
        long id = ID_SEQ.getAndIncrement();
        SSE_REGISTRY.put(id, handler);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment patternSeg = arena.allocateFrom(pattern);
            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) RouterFfi.MH_aegon_router_add_async.invokeExact(
                    routerPtr,
                    Method.GET.nativeCode(),
                    patternSeg,
                    SSE_UPCALL_STUB,
                    userdata,
                    CLEANUP_UPCALL_STUB
            );
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            SSE_REGISTRY.remove(id);
            throw new RuntimeException("Failed to register SSE route: " + t.getMessage(), t);
        }
    }

    /* -------------------------------------------------------------------------
     * Internal Registration Helpers
     * ------------------------------------------------------------------------- */

    static long registerSyncHandler(Handler h) {
        long id = ID_SEQ.getAndIncrement();
        SYNC_REGISTRY.put(id, h);
        return id;
    }

    static void unregisterSyncHandler(long id) {
        SYNC_REGISTRY.remove(id);
    }

    static long registerAsyncHandler(Handler h) {
        long id = ID_SEQ.getAndIncrement();
        ASYNC_REGISTRY.put(id, h);
        return id;
    }

    static void unregisterAsyncHandler(long id) {
        ASYNC_REGISTRY.remove(id);
    }

    static long registerMiddleware(Middleware mw) {
        long id = ID_SEQ.getAndIncrement();
        MIDDLEWARE_REGISTRY.put(id, mw);
        return id;
    }

    static void unregisterMiddleware(long id) {
        MIDDLEWARE_REGISTRY.remove(id);
    }

    static void registerRouterWebSocket(MemorySegment routerPtr, String pattern, WebSocketSession session) {
        long id = ID_SEQ.getAndIncrement();
        WS_SESSION_REGISTRY.put(id, session);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment patSeg = arena.allocateFrom(pattern);
            MemorySegment cbs = arena.allocate(WebSocketFfi.WS_CALLBACKS_LAYOUT);
            WebSocketFfi.MH_aegon_ws_callbacks_init.invokeExact(cbs);

            cbs.set(java.lang.foreign.ValueLayout.ADDRESS, WebSocketFfi.WS_CALLBACKS_LAYOUT.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement("on_open")), WS_ON_OPEN_STUB);
            cbs.set(java.lang.foreign.ValueLayout.ADDRESS, WebSocketFfi.WS_CALLBACKS_LAYOUT.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement("on_message")), WS_ON_MESSAGE_STUB);
            cbs.set(java.lang.foreign.ValueLayout.ADDRESS, WebSocketFfi.WS_CALLBACKS_LAYOUT.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement("on_close")), WS_ON_CLOSE_STUB);
            cbs.set(java.lang.foreign.ValueLayout.ADDRESS, WebSocketFfi.WS_CALLBACKS_LAYOUT.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement("on_error")), WS_ON_ERROR_STUB);

            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) WebSocketFfi.MH_aegon_router_ws.invokeExact(routerPtr, patSeg, cbs, userdata, CLEANUP_UPCALL_STUB);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            WS_SESSION_REGISTRY.remove(id);
            throw new RuntimeException("Failed to register WebSocket endpoint: " + t.getMessage(), t);
        }
    }

    static void registerGroupWebSocket(MemorySegment groupPtr, String pattern, WebSocketSession session) {
        long id = ID_SEQ.getAndIncrement();
        WS_SESSION_REGISTRY.put(id, session);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment patSeg = arena.allocateFrom(pattern);
            MemorySegment cbs = arena.allocate(WebSocketFfi.WS_CALLBACKS_LAYOUT);
            WebSocketFfi.MH_aegon_ws_callbacks_init.invokeExact(cbs);

            cbs.set(java.lang.foreign.ValueLayout.ADDRESS, WebSocketFfi.WS_CALLBACKS_LAYOUT.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement("on_open")), WS_ON_OPEN_STUB);
            cbs.set(java.lang.foreign.ValueLayout.ADDRESS, WebSocketFfi.WS_CALLBACKS_LAYOUT.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement("on_message")), WS_ON_MESSAGE_STUB);
            cbs.set(java.lang.foreign.ValueLayout.ADDRESS, WebSocketFfi.WS_CALLBACKS_LAYOUT.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement("on_close")), WS_ON_CLOSE_STUB);
            cbs.set(java.lang.foreign.ValueLayout.ADDRESS, WebSocketFfi.WS_CALLBACKS_LAYOUT.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement("on_error")), WS_ON_ERROR_STUB);

            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) WebSocketFfi.MH_aegon_group_ws.invokeExact(groupPtr, patSeg, cbs, userdata, CLEANUP_UPCALL_STUB);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            WS_SESSION_REGISTRY.remove(id);
            throw new RuntimeException("Failed to register group WebSocket endpoint: " + t.getMessage(), t);
        }
    }

    /* -------------------------------------------------------------------------
     * Native Upcall Trampolines
     * ------------------------------------------------------------------------- */

    private static void syncTrampoline(MemorySegment ctxPtr, MemorySegment userdataPtr) {
        long id = userdataPtr.address();
        Handler h = SYNC_REGISTRY.get(id);
        if (h != null) {
            Context ctx = new Context(ctxPtr);
            try {
                h.handle(ctx);
            } catch (Throwable t) {
                handleException(ctx, t);
            }
        }
    }

    private static void asyncTrampoline(MemorySegment ctxPtr, MemorySegment userdataPtr) {
        long id = userdataPtr.address();
        Handler h = ASYNC_REGISTRY.get(id);
        if (h == null) return;
        try {
            MemorySegment token = (MemorySegment) AsyncFfi.MH_aegon_async_create.invokeExact(
                    ctxPtr, MemorySegment.NULL, MemorySegment.NULL
            );
            if (token.equals(MemorySegment.NULL)) return;

            Thread.ofVirtual().name("aegon-vt-" + System.nanoTime()).start(() -> {
                try {
                    Context ctx = new Context(ctxPtr);
                    h.handle(ctx);
                } catch (Throwable t) {
                    try {
                        Context ctx = new Context(ctxPtr);
                        handleException(ctx, t);
                    } catch (Throwable ignored) {}
                } finally {
                    try {
                        AsyncFfi.MH_aegon_async_complete.invokeExact(token);
                    } catch (Throwable ignored) {}
                }
            });
        } catch (Throwable ignored) {}
    }

    private static void sseTrampoline(MemorySegment ctxPtr, MemorySegment userdataPtr) {
        long id = userdataPtr.address();
        SseHandler handler = SSE_REGISTRY.get(id);
        if (handler == null) return;
        try {
            MemorySegment ssePtr = (MemorySegment) SseFfi.MH_aegon_sse_create.invokeExact(
                    ctxPtr, MemorySegment.NULL, MemorySegment.NULL
            );
            if (ssePtr.equals(MemorySegment.NULL)) return;

            SseStream stream = new SseStream(ssePtr);
            Context ctx = new Context(ctxPtr);

            Thread.ofVirtual().name("aegon-sse-" + System.nanoTime()).start(() -> {
                try {
                    handler.handle(ctx, stream);
                } catch (Throwable ignored) {
                } finally {
                    if (stream.isOpen()) {
                        stream.close();
                    }
                }
            });
        } catch (Throwable ignored) {}
    }

    private static void cleanupTrampoline(MemorySegment userdataPtr) {
        long id = userdataPtr.address();
        SYNC_REGISTRY.remove(id);
        ASYNC_REGISTRY.remove(id);
        ERROR_REGISTRY.remove(id);
        WS_SESSION_REGISTRY.remove(id);
        SSE_REGISTRY.remove(id);
    }

    private static void errorTrampoline(MemorySegment ctxPtr, MemorySegment whatSeg, MemorySegment userdataPtr) {
        long id = userdataPtr.address();
        ErrorHandler eh = ERROR_REGISTRY.get(id);
        if (eh != null) {
            try {
                Context ctx = new Context(ctxPtr);
                String what = FfiSupport.readStr(whatSeg);
                eh.handleError(ctx, what);
            } catch (Throwable ignored) {}
        }
    }

    private static void middlewareTrampoline(MemorySegment ctxPtr, MemorySegment nextPtr, MemorySegment userdataPtr) {
        long id = userdataPtr.address();
        Middleware mw = MIDDLEWARE_REGISTRY.get(id);
        if (mw != null) {
            Context ctx = new Context(ctxPtr);
            Next next = (context) -> {
                try {
                    MiddlewareFfi.MH_aegon_next_call.invokeExact(nextPtr, context.handle());
                } catch (Throwable t) {
                    throw new RuntimeException(t);
                }
            };
            try {
                mw.handle(ctx, next);
            } catch (Throwable t) {
                handleException(ctx, t);
            }
        }
    }

    private static void middlewareCleanupTrampoline(MemorySegment userdataPtr) {
        long id = userdataPtr.address();
        MIDDLEWARE_REGISTRY.remove(id);
    }

    private static void wsOnOpenTrampoline(MemorySegment wsPtr, MemorySegment pathSeg, MemorySegment userdataPtr) {
        long id = userdataPtr.address();
        WebSocketSession session = WS_SESSION_REGISTRY.get(id);
        if (session != null && session.getOpenHandler() != null) {
            try {
                session.getOpenHandler().accept(new WebSocket(wsPtr));
            } catch (Throwable ignored) {}
        }
    }

    private static void wsOnMessageTrampoline(MemorySegment wsPtr, MemorySegment payloadSeg, int opCode, MemorySegment userdataPtr) {
        long id = userdataPtr.address();
        WebSocketSession session = WS_SESSION_REGISTRY.get(id);
        if (session != null) {
            WebSocket ws = new WebSocket(wsPtr);
            try {
                if (opCode == 0x1 && session.getTextHandler() != null) {
                    session.getTextHandler().accept(ws, FfiSupport.readStr(payloadSeg));
                } else if (session.getBinaryHandler() != null) {
                    session.getBinaryHandler().accept(ws, FfiSupport.readStrBytes(payloadSeg));
                }
            } catch (Throwable ignored) {}
        }
    }

    private static void wsOnCloseTrampoline(MemorySegment wsPtr, short code, MemorySegment reasonSeg, MemorySegment userdataPtr) {
        long id = userdataPtr.address();
        WebSocketSession session = WS_SESSION_REGISTRY.get(id);
        if (session != null && session.getCloseHandler() != null) {
            try {
                session.getCloseHandler().handle(new WebSocket(wsPtr), code & 0xFFFF, FfiSupport.readStr(reasonSeg));
            } catch (Throwable ignored) {}
        }
    }

    private static void wsOnErrorTrampoline(MemorySegment wsPtr, MemorySegment whatSeg, MemorySegment userdataPtr) {
        long id = userdataPtr.address();
        WebSocketSession session = WS_SESSION_REGISTRY.get(id);
        if (session != null && session.getErrorHandler() != null) {
            try {
                session.getErrorHandler().accept(new WebSocket(wsPtr), FfiSupport.readStr(whatSeg));
            } catch (Throwable ignored) {}
        }
    }

    private static void handleException(Context ctx, Throwable t) {
        if (globalErrorHandler != null) {
            try {
                globalErrorHandler.handleError(ctx, t.getMessage() != null ? t.getMessage() : t.toString());
                return;
            } catch (Throwable ignored) {}
        }
        String msg = t.getMessage() != null ? t.getMessage() : t.toString();
        ctx.problem(StatusCode.INTERNAL_SERVER_ERROR, "Internal Server Error", msg);
    }

    private void checkNotClosed() {
        if (routerPtr == null || routerPtr.equals(MemorySegment.NULL)) {
            throw new IllegalStateException("Router has already been consumed by an AegonServer or destroyed");
        }
    }

    /**
     * Marks this router instance consumed (used when moving into {@link com.teamzemo.aegon.AegonServer}).
     */
    public void markConsumed() {
        this.routerPtr = MemorySegment.NULL;
    }

    @Override
    public void close() {
        if (routerPtr != null && !routerPtr.equals(MemorySegment.NULL)) {
            try {
                RouterFfi.MH_aegon_router_destroy.invokeExact(routerPtr);
            } catch (Throwable ignored) {}
            routerPtr = MemorySegment.NULL;
        }
    }
}
