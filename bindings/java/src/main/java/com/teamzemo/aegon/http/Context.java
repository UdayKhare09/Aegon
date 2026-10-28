package com.teamzemo.aegon.http;

import com.teamzemo.aegon.internal.ffi.ContextFfi;
import com.teamzemo.aegon.internal.ffi.FfiSupport;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Unified HTTP exchange context passed to route handlers and middleware matching Aegon C++ {@code aegon::http::Context&}.
 *
 * <p>Encapsulates the inbound {@link Request}, the outbound {@link Response},
 * RFC 7807 problem details generation, and per-request middleware locals bag.
 */
public final class Context {

    private static final ConcurrentHashMap<Long, ConcurrentHashMap<String, Object>> REQUEST_LOCALS = new ConcurrentHashMap<>();

    private final MemorySegment ctxPtr;
    private final Request request;
    private final Response response;

    public Context(MemorySegment ctxPtr) {
        this.ctxPtr = Objects.requireNonNull(ctxPtr, "ctxPtr cannot be null");
        try {
            MemorySegment reqPtr = (MemorySegment) ContextFfi.MH_aegon_ctx_req.invokeExact(ctxPtr);
            MemorySegment resPtr = (MemorySegment) ContextFfi.MH_aegon_ctx_res.invokeExact(ctxPtr);
            this.request = new Request(reqPtr, ctxPtr);
            this.response = new Response(resPtr);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to initialize Context accessors: " + t.getMessage(), t);
        }
    }

    /**
     * Returns the native pointer to this {@code aegon_ctx_t}.
     *
     * @return memory segment pointer
     */
    public MemorySegment handle() {
        return ctxPtr;
    }

    /**
     * Accesses the incoming HTTP request envelope.
     *
     * @return {@link Request}
     */
    public Request req() {
        return request;
    }

    /**
     * Accesses the outgoing HTTP response builder.
     *
     * @return {@link Response}
     */
    public Response res() {
        return response;
    }

    /**
     * Emits a standardized RFC 7807 Problem Details error response.
     *
     * <p>Sets {@code Content-Type: application/problem+json}, HTTP status code,
     * and formats a JSON payload with title, status, and detail.
     *
     * @param status HTTP status code enum
     * @param title short human-readable summary
     * @param detail detailed explanation of the error
     */
    public void problem(StatusCode status, String title, String detail) {
        problem(status.code(), title, detail);
    }

    /**
     * Emits a standardized RFC 7807 Problem Details error response with a numeric status code.
     *
     * @param status numeric status code (e.g. 400, 404, 500)
     * @param title short human-readable summary
     * @param detail detailed explanation of the error
     */
    public void problem(int status, String title, String detail) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment titleSeg = FfiSupport.makeStr(arena, title);
            MemorySegment detailSeg = FfiSupport.makeStr(arena, detail);
            int err = (int) ContextFfi.MH_aegon_ctx_problem.invokeExact(ctxPtr, (short) status, titleSeg, detailSeg);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to set problem details: " + t.getMessage(), t);
        }
    }

    /**
     * Sends a static file from the filesystem with automatic MIME detection.
     *
     * @param path path to file on disk
     */
    public void sendFile(String path) {
        sendFile(path, null);
    }

    /**
     * Sends a static file from the filesystem with an explicit MIME type.
     *
     * @param path path to file on disk
     * @param mime MIME content type, or null to infer from extension
     */
    public void sendFile(String path, String mime) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment pathSeg = arena.allocateFrom(path);
            MemorySegment mimeSeg = FfiSupport.makeStr(arena, mime);
            int err = (int) ContextFfi.MH_aegon_ctx_send_file.invokeExact(ctxPtr, pathSeg, mimeSeg);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to send file: " + t.getMessage(), t);
        }
    }

    /**
     * Stores a per-request variable in this exchange context for downstream handlers or middleware.
     *
     * @param key variable key
     * @param value variable value
     */
    public void set(String key, Object value) {
        if (key != null) {
            REQUEST_LOCALS.computeIfAbsent(ctxPtr.address(), k -> new ConcurrentHashMap<>()).put(key, value);
        }
    }

    /**
     * Retrieves a per-request variable by key.
     *
     * @param key variable key
     * @param <T> expected type
     * @return variable value, or {@code null} if not found
     */
    @SuppressWarnings("unchecked")
    public <T> T get(String key) {
        ConcurrentHashMap<String, Object> map = REQUEST_LOCALS.get(ctxPtr.address());
        return map != null ? (T) map.get(key) : null;
    }

    /**
     * Clears cached locals for this context address.
     */
    public void clearLocals() {
        REQUEST_LOCALS.remove(ctxPtr.address());
    }

    /**
     * Retrieves the application-level data pointer set on the server via {@code server.setAppData(...)}.
     *
     * @param <T> expected type
     * @return application data instance, or {@code null}
     */
    @SuppressWarnings("unchecked")
    public <T> T appData() {
        return (T) AegonServerRegistry.getAppData();
    }

    /**
     * Convenience method to deserialize and bind the request body to a typed Java class or record.
     *
     * @param targetType target class or record
     * @param <T> type
     * @return deserialized instance
     */
    public <T> T bindJson(Class<T> targetType) {
        return req().json(targetType);
    }

    /**
     * Sets the outbound HTTP response status code.
     *
     * @param status status code enum
     * @return this {@link Context} for chaining
     */
    public Context status(StatusCode status) {
        res().status(status);
        return this;
    }

    /**
     * Sets the outbound HTTP response status code.
     *
     * @param statusCode numeric status code
     * @return this {@link Context} for chaining
     */
    public Context status(int statusCode) {
        res().status(statusCode);
        return this;
    }

    /**
     * Sends plain text payload with {@code Content-Type: text/plain; charset=utf-8}.
     *
     * @param text plain text content
     */
    public void text(String text) {
        res().text(text);
    }

    /**
     * Serializes object or record to JSON and sends with {@code Content-Type: application/json; charset=utf-8}.
     *
     * @param obj object to serialize
     */
    public void json(Object obj) {
        res().json(obj);
    }

    /**
     * Sends HTML payload with {@code Content-Type: text/html; charset=utf-8}.
     *
     * @param html HTML markup
     */
    public void html(String html) {
        res().html(html);
    }

    /**
     * Extracts named route parameter value from URL.
     *
     * @param name parameter name without colon (e.g. {@code "id"})
     * @return parameter value, or {@code null}
     */
    public String param(String name) {
        return req().param(name).orElse(null);
    }

    /**
     * Extracts named route parameter value from URL as an {@link java.util.Optional}.
     *
     * @param name parameter name without colon
     * @return optional parameter value
     */
    public java.util.Optional<String> paramOpt(String name) {
        return req().param(name);
    }

    /**
     * Extracts query parameter value by name.
     *
     * @param name query parameter name
     * @return parameter value, or {@code null}
     */
    public String query(String name) {
        return req().query(name).orElse(null);
    }

    /**
     * Extracts query parameter value by name as an {@link java.util.Optional}.
     *
     * @param name query parameter name
     * @return optional query parameter value
     */
    public java.util.Optional<String> queryOpt(String name) {
        return req().query(name);
    }

    /**
     * Reads the entire inbound request body as a UTF-8 string.
     *
     * @return request body string
     */
    public String body() {
        return req().body();
    }

    /**
     * Reads the entire inbound request body as a byte array.
     *
     * @return byte array
     */
    public byte[] bodyBytes() {
        return req().bodyBytes();
    }
}
