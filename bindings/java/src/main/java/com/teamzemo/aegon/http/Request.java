package com.teamzemo.aegon.http;

import com.teamzemo.aegon.internal.ffi.CoreFfi;
import com.teamzemo.aegon.internal.ffi.FfiSupport;
import com.teamzemo.aegon.internal.ffi.RequestFfi;
import com.teamzemo.aegon.json.Json;
import com.teamzemo.aegon.multipart.MultipartFormData;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SegmentAllocator;
import java.util.*;

/**
 * Incoming HTTP Request envelope matching Aegon C++ {@code aegon::http::Request}.
 *
 * <p>Provides zero-copy views into the underlying socket receive buffer,
 * fast parameter extraction, and JSON deserialization via {@link Json}.
 */
public final class Request {

    private final MemorySegment reqPtr;
    private final MemorySegment ctxPtr;

    public Request(MemorySegment reqPtr, MemorySegment ctxPtr) {
        this.reqPtr = Objects.requireNonNull(reqPtr, "reqPtr cannot be null");
        this.ctxPtr = ctxPtr;
    }

    /**
     * Returns the native pointer to this {@code aegon_req_t}.
     *
     * @return memory segment pointer
     */
    public MemorySegment handle() {
        return reqPtr;
    }

    /**
     * HTTP Request Method (e.g. GET, POST, PUT, DELETE).
     *
     * @return {@link Method} enum constant
     */
    public Method method() {
        try {
            int m = (int) RequestFfi.MH_aegon_req_method.invokeExact(reqPtr);
            return Method.fromNativeCode(m);
        } catch (Throwable t) {
            return Method.UNKNOWN;
        }
    }

    /**
     * HTTP Protocol Version (e.g. HTTP/1.1, HTTP/2.0, HTTP/3.0).
     *
     * @return {@link HttpVersion} enum constant
     */
    public HttpVersion version() {
        try {
            int v = (int) RequestFfi.MH_aegon_req_version.invokeExact(reqPtr);
            return HttpVersion.fromNativeCode(v);
        } catch (Throwable t) {
            return HttpVersion.HTTP_1_1;
        }
    }

    /**
     * URL path of the request (e.g. "/api/v1/users").
     *
     * @return request path
     */
    public String path() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment strSeg = (MemorySegment) RequestFfi.MH_aegon_req_path.invokeExact((SegmentAllocator) arena, reqPtr);
            return FfiSupport.readStr(strSeg);
        } catch (Throwable t) {
            return "";
        }
    }

    /**
     * Raw query string without the leading '?' (e.g. {@code "page=1&limit=25"}).
     *
     * @return query string, or empty string if absent
     */
    public String query() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment strSeg = (MemorySegment) RequestFfi.MH_aegon_req_query_string.invokeExact((SegmentAllocator) arena, reqPtr);
            return FfiSupport.readStr(strSeg);
        } catch (Throwable t) {
            return "";
        }
    }

    /**
     * Alias for {@link #query()} to match C++ {@code req.query()}.
     *
     * @return query string
     */
    public String queryString() {
        return query();
    }

    /**
     * Parses and returns a single query parameter by key from the query string.
     *
     * @param key query parameter name
     * @return {@link Optional} containing parameter value, or empty if absent
     */
    public Optional<String> query(String key) {
        if (key == null || key.isEmpty()) return Optional.empty();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment keySeg = FfiSupport.makeStr(arena, key);
            MemorySegment strSeg = (MemorySegment) RequestFfi.MH_aegon_req_query.invokeExact((SegmentAllocator) arena, reqPtr, keySeg);
            String val = FfiSupport.readStr(strSeg);
            return (val != null && !val.isEmpty()) ? Optional.of(val) : Optional.empty();
        } catch (Throwable t) {
            return Optional.empty();
        }
    }

    /**
     * Parses an integer query parameter with a fallback default value.
     *
     * @param key parameter key
     * @param defaultValue default value if absent or not an integer
     * @return parsed integer or default value
     */
    public int queryInt(String key, int defaultValue) {
        return query(key).map(v -> {
            try {
                return Integer.parseInt(v);
            } catch (NumberFormatException e) {
                return defaultValue;
            }
        }).orElse(defaultValue);
    }

    /**
     * Raw request body as a UTF-8 string.
     *
     * @return request body string
     */
    public String body() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment strSeg = (MemorySegment) RequestFfi.MH_aegon_req_body.invokeExact((SegmentAllocator) arena, reqPtr);
            return FfiSupport.readStr(strSeg);
        } catch (Throwable t) {
            return "";
        }
    }

    /**
     * Raw request body bytes.
     *
     * @return byte array
     */
    public byte[] bodyBytes() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment strSeg = (MemorySegment) RequestFfi.MH_aegon_req_body.invokeExact((SegmentAllocator) arena, reqPtr);
            return FfiSupport.readStrBytes(strSeg);
        } catch (Throwable t) {
            return new byte[0];
        }
    }

    /**
     * Direct zero-copy {@link MemorySegment} slice of the request body in off-heap memory.
     *
     * @return off-heap memory segment
     */
    public MemorySegment bodySegment() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment strSeg = (MemorySegment) RequestFfi.MH_aegon_req_body.invokeExact((SegmentAllocator) arena, reqPtr);
            return FfiSupport.readStrSegment(strSeg);
        } catch (Throwable t) {
            return MemorySegment.NULL;
        }
    }

    /**
     * Retrieves an HTTP request header by name (case-insensitive).
     *
     * @param name header name
     * @return {@link Optional} containing header value, or empty if absent
     */
    public Optional<String> header(String name) {
        if (name == null || name.isEmpty()) return Optional.empty();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nameSeg = FfiSupport.makeStr(arena, name);
            MemorySegment strSeg = (MemorySegment) RequestFfi.MH_aegon_req_header.invokeExact((SegmentAllocator) arena, reqPtr, nameSeg);
            String val = FfiSupport.readStr(strSeg);
            return (val != null && !val.isEmpty()) ? Optional.of(val) : Optional.empty();
        } catch (Throwable t) {
            return Optional.empty();
        }
    }

    /**
     * Checks if the request contains the specified header name (case-insensitive).
     *
     * @param name header name
     * @return true if present, false otherwise
     */
    public boolean hasHeader(String name) {
        if (name == null || name.isEmpty()) return false;
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nameSeg = FfiSupport.makeStr(arena, name);
            return (boolean) RequestFfi.MH_aegon_req_has_header.invokeExact(reqPtr, nameSeg);
        } catch (Throwable t) {
            return false;
        }
    }

    /**
     * Returns an unmodifiable map of all incoming HTTP headers in arrival order.
     *
     * @return map of header names to values
     */
    public Map<String, String> headers() {
        Map<String, String> map = new LinkedHashMap<>();
        try (Arena arena = Arena.ofConfined()) {
            long count = (long) RequestFfi.MH_aegon_req_header_count.invokeExact(reqPtr);
            MemorySegment outName = arena.allocate(FfiSupport.STR_LAYOUT);
            MemorySegment outVal = arena.allocate(FfiSupport.STR_LAYOUT);
            for (long i = 0; i < count; i++) {
                boolean ok = (boolean) RequestFfi.MH_aegon_req_header_at.invokeExact(reqPtr, i, outName, outVal);
                if (ok) {
                    map.put(FfiSupport.readStr(outName), FfiSupport.readStr(outVal));
                }
            }
        } catch (Throwable ignored) {}
        return Collections.unmodifiableMap(map);
    }

    /**
     * Retrieves a named path parameter extracted from dynamic route patterns (e.g. {@code /users/:id}).
     *
     * @param key parameter name (e.g. "id")
     * @return {@link Optional} containing the matched value, or empty if not present
     */
    public Optional<String> param(String key) {
        if (key == null || key.isEmpty()) return Optional.empty();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment keySeg = FfiSupport.makeStr(arena, key);
            MemorySegment strSeg = (MemorySegment) RequestFfi.MH_aegon_req_param.invokeExact((SegmentAllocator) arena, reqPtr, keySeg);
            String val = FfiSupport.readStr(strSeg);
            return (val != null && !val.isEmpty()) ? Optional.of(val) : Optional.empty();
        } catch (Throwable t) {
            return Optional.empty();
        }
    }

    /**
     * Returns all matched route parameters as a key-value map.
     *
     * @return map of route parameter names to values
     */
    public Map<String, String> params() {
        Map<String, String> map = new LinkedHashMap<>();
        try (Arena arena = Arena.ofConfined()) {
            long count = (long) RequestFfi.MH_aegon_req_param_count.invokeExact(reqPtr);
            MemorySegment outKey = arena.allocate(FfiSupport.STR_LAYOUT);
            MemorySegment outVal = arena.allocate(FfiSupport.STR_LAYOUT);
            for (long i = 0; i < count; i++) {
                boolean ok = (boolean) RequestFfi.MH_aegon_req_param_at.invokeExact(reqPtr, i, outKey, outVal);
                if (ok) {
                    map.put(FfiSupport.readStr(outKey), FfiSupport.readStr(outVal));
                }
            }
        } catch (Throwable ignored) {}
        return Collections.unmodifiableMap(map);
    }

    /**
     * Parses and returns the value of an HTTP Cookie from the {@code Cookie} header.
     *
     * @param name cookie name
     * @return {@link Optional} containing cookie value, or empty if absent
     */
    public Optional<String> cookie(String name) {
        if (name == null || name.isEmpty()) return Optional.empty();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nameSeg = FfiSupport.makeStr(arena, name);
            MemorySegment strSeg = (MemorySegment) RequestFfi.MH_aegon_req_cookie.invokeExact((SegmentAllocator) arena, reqPtr, nameSeg);
            String val = FfiSupport.readStr(strSeg);
            return (val != null && !val.isEmpty()) ? Optional.of(val) : Optional.empty();
        } catch (Throwable t) {
            return Optional.empty();
        }
    }

    /**
     * Checks if the incoming request is a gRPC call.
     *
     * @return true if Content-Type starts with application/grpc
     */
    public boolean isGrpc() {
        try {
            return (boolean) RequestFfi.MH_aegon_req_is_grpc.invokeExact(reqPtr);
        } catch (Throwable t) {
            return false;
        }
    }

    /**
     * Checks if the incoming request is an RFC 6455 WebSocket upgrade handshake.
     *
     * @return true if Upgrade: websocket header is present
     */
    public boolean isWebSocket() {
        try {
            return (boolean) RequestFfi.MH_aegon_req_is_websocket.invokeExact(reqPtr);
        } catch (Throwable t) {
            return false;
        }
    }

    /**
     * Helper to URL-decode a percent-encoded string using Aegon's SIMD URL decoder.
     *
     * @param in encoded string
     * @return decoded string
     */
    public static String urlDecode(String in) {
        if (in == null || in.isEmpty()) return "";
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment inSeg = FfiSupport.makeStr(arena, in);
            MemorySegment outSeg = (MemorySegment) CoreFfi.MH_aegon_url_decode.invokeExact((SegmentAllocator) arena, inSeg);
            String res = FfiSupport.readStr(outSeg);
            MemorySegment dataPtr = outSeg.get(java.lang.foreign.ValueLayout.ADDRESS, FfiSupport.STR_DATA_OFFSET);
            if (!dataPtr.equals(MemorySegment.NULL)) {
                CoreFfi.MH_aegon_mem_free.invokeExact(dataPtr);
            }
            return res;
        } catch (Throwable t) {
            return in;
        }
    }

    /**
     * Deserializes the request body JSON into a typed Java class or record using the active JSON binder.
     *
     * @param targetType target class or record
     * @param <T> type
     * @return deserialized instance
     */
    public <T> T json(Class<T> targetType) {
        return Json.decode(bodySegment(), targetType);
    }

    /**
     * Parses multipart/form-data payload and files if this request has Content-Type multipart/form-data.
     *
     * @return parsed {@link MultipartFormData}, or {@code null} if not multipart
     */
    public MultipartFormData multipart() {
        if (ctxPtr == null || ctxPtr.equals(MemorySegment.NULL)) {
            return null;
        }
        return MultipartFormData.parse(ctxPtr);
    }
}
