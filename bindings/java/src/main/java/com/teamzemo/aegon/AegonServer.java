package com.teamzemo.aegon;

import com.teamzemo.aegon.http.AegonServerRegistry;
import com.teamzemo.aegon.http.Router;
import com.teamzemo.aegon.internal.ffi.FfiSupport;
import com.teamzemo.aegon.internal.ffi.ServerFfi;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;

/**
 * Top-level Aegon HTTP/1.1, HTTP/2, and HTTP/3 Web Server matching Aegon C++ {@code aegon::http::Server}.
 *
 * <p>Encapsulates multi-port plaintext and TLS/QUIC listeners, thread-per-core event loops,
 * Project Loom VirtualThread dispatching, and high-performance routing.
 *
 * <pre>{@code
 * AegonServer server = AegonServer.create();
 *
 * server.router().get("/health", ctx -> {
 *     ctx.res().status(StatusCode.OK).text("OK");
 * });
 *
 * server.listen(8080);
 * server.run(); // Launches thread-per-core reactor cluster
 * }</pre>
 */
public final class AegonServer implements AutoCloseable {

    private final Router router;
    private final ServerConfig config;
    private final List<Listener> listeners = new ArrayList<>();
    private String tlsCertFile;
    private String tlsKeyFile;
    private MemorySegment serverPtr;
    private Arena serverArena;
    private volatile boolean running;

    private static final AtomicLong CB_SEQ = new AtomicLong(1);
    private static final ConcurrentHashMap<Long, Consumer<AegonServer>> LIFECYCLE_REGISTRY = new ConcurrentHashMap<>();
    private static final ConcurrentHashMap<Long, Runnable> WORKER_REGISTRY = new ConcurrentHashMap<>();
    private static final ConcurrentHashMap<Long, AegonServer> SERVER_INSTANCE_MAP = new ConcurrentHashMap<>();

    private static final MemorySegment LIFECYCLE_STUB;
    private static final MemorySegment WORKER_STUB;

    static {
        try {
            MethodHandles.Lookup lookup = MethodHandles.lookup();
            MethodHandle mhLife = lookup.findStatic(AegonServer.class, "lifecycleTrampoline",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class));
            LIFECYCLE_STUB = FfiSupport.LINKER.upcallStub(mhLife, ServerFfi.LIFECYCLE_UPCALL_DESC, Arena.global());

            MethodHandle mhWork = lookup.findStatic(AegonServer.class, "workerTrampoline",
                    MethodType.methodType(void.class, MemorySegment.class));
            WORKER_STUB = FfiSupport.LINKER.upcallStub(mhWork, ServerFfi.WORKER_UPCALL_DESC, Arena.global());
        } catch (Throwable t) {
            throw new ExceptionInInitializerError("Failed to initialize AegonServer upcall stubs: " + t.getMessage());
        }
    }

    private static void lifecycleTrampoline(MemorySegment serverPtr, MemorySegment userdataPtr) {
        long id = userdataPtr.address();
        Consumer<AegonServer> cb = LIFECYCLE_REGISTRY.get(id);
        if (cb != null) {
            try {
                AegonServer s = SERVER_INSTANCE_MAP.get(serverPtr.address());
                cb.accept(s);
            } catch (Throwable ignored) {}
        }
    }

    private static void workerTrampoline(MemorySegment userdataPtr) {
        long id = userdataPtr.address();
        Runnable r = WORKER_REGISTRY.get(id);
        if (r != null) {
            try {
                r.run();
            } catch (Throwable ignored) {}
        }
    }

    private record Listener(int port, String host, boolean isTls) {}

    public AegonServer() {
        this(Router.create(), ServerConfig.defaults());
    }

    public AegonServer(Router router) {
        this(router, ServerConfig.defaults());
    }

    public AegonServer(Router router, ServerConfig config) {
        this.router = Objects.requireNonNull(router, "router cannot be null");
        this.config = Objects.requireNonNull(config, "config cannot be null");
    }

    /**
     * Creates a new {@link AegonServer} instance with default configuration.
     *
     * @return fresh {@link AegonServer}
     */
    public static AegonServer create() {
        return new AegonServer();
    }

    /**
     * Creates a new {@link AegonServer} with a pre-configured {@link Router}.
     *
     * @param router configured {@link Router}
     * @return fresh {@link AegonServer}
     */
    public static AegonServer create(Router router) {
        return new AegonServer(router);
    }

    /**
     * Creates a new {@link AegonServer} with custom {@link ServerConfig} and a fresh {@link Router}.
     *
     * @param config server configuration
     * @return fresh {@link AegonServer}
     */
    public static AegonServer create(ServerConfig config) {
        return new AegonServer(Router.create(), config);
    }

    /**
     * Creates a new {@link AegonServer} with custom {@link Router} and {@link ServerConfig}.
     *
     * @param router configured {@link Router}
     * @param config server configuration
     * @return fresh {@link AegonServer}
     */
    public static AegonServer create(Router router, ServerConfig config) {
        return new AegonServer(router, config);
    }

    /**
     * Accesses the server's HTTP router.
     *
     * @return {@link Router}
     */
    public Router router() {
        return router;
    }

    /**
     * Binds a plaintext HTTP listener on the specified port on "0.0.0.0".
     *
     * @param port TCP port (1..65535)
     * @return {@code this} server builder
     */
    public AegonServer listen(int port) {
        return listen(port, "0.0.0.0");
    }

    /**
     * Binds a plaintext HTTP listener on the specified port and host address.
     *
     * @param port TCP port (1..65535)
     * @param host bind address (e.g. "0.0.0.0" or "127.0.0.1")
     * @return {@code this} server builder
     */
    public AegonServer listen(int port, String host) {
        validatePort(port);
        checkNotRunning();
        listeners.add(new Listener(port, host, false));
        return this;
    }

    /**
     * Binds an encrypted TLS / HTTP/2 / HTTP/3 listener on the specified port on "0.0.0.0".
     *
     * @param port TCP/UDP port (1..65535)
     * @return {@code this} server builder
     */
    public AegonServer listenTls(int port) {
        return listenTls(port, "0.0.0.0");
    }

    /**
     * Binds an encrypted TLS / HTTP/2 / HTTP/3 listener on the specified port and host address.
     *
     * @param port TCP/UDP port (1..65535)
     * @param host bind address
     * @return {@code this} server builder
     */
    public AegonServer listenTls(int port, String host) {
        validatePort(port);
        checkNotRunning();
        listeners.add(new Listener(port, host, true));
        return this;
    }

    /**
     * Enables TLS encryption with the given certificate and private key files.
     *
     * @param certFile filesystem path to PEM certificate file
     * @param keyFile filesystem path to PEM private key file
     * @return {@code this} server builder
     */
    public AegonServer enableTls(String certFile, String keyFile) {
        checkNotRunning();
        this.tlsCertFile = certFile;
        this.tlsKeyFile = keyFile;
        return this;
    }

    /**
     * Sets application-level shared data accessible from all handlers via {@code ctx.appData()}.
     *
     * @param appData shared application data instance
     * @return {@code this} server builder
     */
    public AegonServer setAppData(Object appData) {
        checkNotRunning();
        AegonServerRegistry.setAppData(appData);
        return this;
    }

    /**
     * Registers a callback invoked when the server finishes binding and starts accepting connections.
     *
     * @param callback lifecycle callback
     * @return this {@link AegonServer}
     */
    public AegonServer onStart(Consumer<AegonServer> callback) {
        checkNotRunning();
        initNativeServer();
        long id = CB_SEQ.getAndIncrement();
        LIFECYCLE_REGISTRY.put(id, callback);
        try {
            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) ServerFfi.MH_aegon_server_on_start.invokeExact(
                    serverPtr, LIFECYCLE_STUB, userdata, Router.CLEANUP_UPCALL_STUB
            );
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            LIFECYCLE_REGISTRY.remove(id);
            throw new RuntimeException("Failed to register onStart handler: " + t.getMessage(), t);
        }
    }

    /**
     * Registers a callback invoked when the server begins shutting down.
     *
     * @param callback lifecycle callback
     * @return this {@link AegonServer}
     */
    public AegonServer onStop(Consumer<AegonServer> callback) {
        checkNotRunning();
        initNativeServer();
        long id = CB_SEQ.getAndIncrement();
        LIFECYCLE_REGISTRY.put(id, callback);
        try {
            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) ServerFfi.MH_aegon_server_on_stop.invokeExact(
                    serverPtr, LIFECYCLE_STUB, userdata, Router.CLEANUP_UPCALL_STUB
            );
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            LIFECYCLE_REGISTRY.remove(id);
            throw new RuntimeException("Failed to register onStop handler: " + t.getMessage(), t);
        }
    }

    /**
     * Spawns a background worker thread associated with the server's lifecycle.
     *
     * @param worker runnable task
     * @return this {@link AegonServer}
     */
    public AegonServer spawnWorker(Runnable worker) {
        checkNotRunning();
        initNativeServer();
        long id = CB_SEQ.getAndIncrement();
        WORKER_REGISTRY.put(id, worker);
        try {
            MemorySegment userdata = MemorySegment.ofAddress(id);
            int err = (int) ServerFfi.MH_aegon_server_spawn_worker.invokeExact(
                    serverPtr, WORKER_STUB, userdata, Router.CLEANUP_UPCALL_STUB
            );
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            WORKER_REGISTRY.remove(id);
            throw new RuntimeException("Failed to register worker: " + t.getMessage(), t);
        }
    }

    /**
     * Starts the server blocking on all available CPU cores (hardware concurrency).
     */
    public void run() {
        run(Runtime.getRuntime().availableProcessors());
    }

    /**
     * Starts the server blocking with the specified number of worker threads.
     *
     * @param threads number of thread-per-core event loops
     */
    public void run(int threads) {
        initNativeServer();
        running = true;
        try {
            int err = (int) ServerFfi.MH_aegon_server_run.invokeExact(serverPtr, threads);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            running = false;
            throw new RuntimeException("Server run failed: " + t.getMessage(), t);
        } finally {
            running = false;
        }
    }

    /**
     * Starts the server asynchronously on a background supervisor thread across all CPU cores.
     */
    public void runAsync() {
        runAsync(Runtime.getRuntime().availableProcessors());
    }

    /**
     * Starts the server asynchronously on a background supervisor thread with the given worker count.
     *
     * @param threads number of worker threads
     */
    public void runAsync(int threads) {
        initNativeServer();
        try {
            int err = (int) ServerFfi.MH_aegon_server_run_async.invokeExact(serverPtr, threads);
            FfiSupport.checkErr(err);
            running = true;
        } catch (Throwable t) {
            running = false;
            throw new RuntimeException("Server runAsync failed: " + t.getMessage(), t);
        }
    }

    /**
     * Signals the server to stop accepting new requests and gracefully drain active connections.
     */
    public void stop() {
        if (serverPtr != null && !serverPtr.equals(MemorySegment.NULL)) {
            try {
                ServerFfi.MH_aegon_server_stop.invokeExact(serverPtr);
            } catch (Throwable ignored) {}
            running = false;
        }
    }

    /**
     * Blocks the calling thread until the asynchronous server supervisor thread stops.
     */
    public void join() {
        if (serverPtr != null && !serverPtr.equals(MemorySegment.NULL)) {
            try {
                int err = (int) ServerFfi.MH_aegon_server_join.invokeExact(serverPtr);
                FfiSupport.checkErr(err);
            } catch (Throwable t) {
                throw new RuntimeException("Failed to join server: " + t.getMessage(), t);
            }
        }
    }

    /**
     * Checks if the server is currently accepting connections.
     *
     * @return true if running, false otherwise
     */
    public boolean isRunning() {
        if (serverPtr != null && !serverPtr.equals(MemorySegment.NULL)) {
            try {
                return (boolean) ServerFfi.MH_aegon_server_is_running.invokeExact(serverPtr);
            } catch (Throwable ignored) {}
        }
        return running;
    }

    private void initNativeServer() {
        if (serverPtr != null && !serverPtr.equals(MemorySegment.NULL)) {
            return;
        }
        this.serverArena = Arena.ofShared();
        try {
            MemorySegment routerPtrPtr = serverArena.allocate(ValueLayout.ADDRESS);
            routerPtrPtr.set(ValueLayout.ADDRESS, 0, router.handle());

            MemorySegment cfgSeg = config.toNative(serverArena);
            this.serverPtr = (MemorySegment) ServerFfi.MH_aegon_server_create.invokeExact(routerPtrPtr, cfgSeg);
            if (serverPtr.equals(MemorySegment.NULL)) {
                throw new RuntimeException("Failed to allocate native AegonServer");
            }
            SERVER_INSTANCE_MAP.put(serverPtr.address(), this);
            router.markConsumed();

            // Enable TLS if configured
            if (tlsCertFile != null && tlsKeyFile != null) {
                MemorySegment certSeg = serverArena.allocateFrom(tlsCertFile);
                MemorySegment keySeg = serverArena.allocateFrom(tlsKeyFile);
                int err = (int) ServerFfi.MH_aegon_server_enable_tls.invokeExact(serverPtr, certSeg, keySeg);
                FfiSupport.checkErr(err);
            }

            // Register listeners (default to 8080 if none registered)
            if (listeners.isEmpty()) {
                listeners.add(new Listener(8080, "0.0.0.0", false));
            }

            for (Listener l : listeners) {
                MemorySegment hostSeg = l.host != null ? serverArena.allocateFrom(l.host) : MemorySegment.NULL;
                if (l.isTls) {
                    int err = (int) ServerFfi.MH_aegon_server_listen_tls.invokeExact(serverPtr, (short) l.port, hostSeg);
                    FfiSupport.checkErr(err);
                } else {
                    int err = (int) ServerFfi.MH_aegon_server_listen.invokeExact(serverPtr, (short) l.port, hostSeg);
                    FfiSupport.checkErr(err);
                }
            }
        } catch (Throwable t) {
            throw new RuntimeException("Failed to initialize AegonServer: " + t.getMessage(), t);
        }
    }

    private void validatePort(int port) {
        if (port < 1 || port > 65535) {
            throw new IllegalArgumentException("Port must be between 1 and 65535, got: " + port);
        }
    }

    private void checkNotRunning() {
        if (running) {
            throw new IllegalStateException("Cannot reconfigure AegonServer while it is running");
        }
    }

    @Override
    public void close() {
        stop();
        if (serverPtr != null && !serverPtr.equals(MemorySegment.NULL)) {
            SERVER_INSTANCE_MAP.remove(serverPtr.address());
            try {
                ServerFfi.MH_aegon_server_destroy.invokeExact(serverPtr);
            } catch (Throwable ignored) {}
            serverPtr = MemorySegment.NULL;
        }
        if (serverArena != null) {
            try {
                serverArena.close();
            } catch (Throwable ignored) {}
            serverArena = null;
        }
    }
}
