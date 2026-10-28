package com.teamzemo.aegon.http;

import com.teamzemo.aegon.internal.ffi.FfiSupport;
import com.teamzemo.aegon.internal.ffi.ResponseFfi;
import com.teamzemo.aegon.json.Json;

import java.lang.foreign.Arena;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.Objects;

/**
 * Fluent HTTP Response Builder matching Aegon C++ {@code aegon::http::Response}.
 *
 * <p>All mutator methods return {@code this} for chainable response construction:
 * <pre>{@code
 * ctx.res()
 *    .status(StatusCode.OK)
 *    .header("X-Custom", "value")
 *    .json(userRecord);
 * }</pre>
 */
public final class Response {

    private final MemorySegment resPtr;

    public Response(MemorySegment resPtr) {
        this.resPtr = Objects.requireNonNull(resPtr, "resPtr cannot be null");
    }

    /**
     * Returns the native pointer to this {@code aegon_res_t}.
     *
     * @return native memory segment pointer
     */
    public MemorySegment handle() {
        return resPtr;
    }

    /**
     * Sets the HTTP response status code using {@link StatusCode}.
     *
     * @param code status code enum constant
     * @return {@code this} response builder
     */
    public Response status(StatusCode code) {
        return status(code.code());
    }

    /**
     * Sets the HTTP response status code using a raw 3-digit integer.
     *
     * @param code numeric status code (e.g. 200, 404, 500)
     * @return {@code this} response builder
     */
    public Response status(int code) {
        try {
            ResponseFfi.MH_aegon_res_status.invokeExact(resPtr, (short) code);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to set status: " + t.getMessage(), t);
        }
    }

    /**
     * Sets an HTTP response header, copying name and value into owned native storage.
     *
     * @param name header name
     * @param value header value
     * @return {@code this} response builder
     */
    public Response header(String name, String value) {
        if (name == null || value == null) return this;
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nameSeg = FfiSupport.makeStr(arena, name);
            MemorySegment valSeg = FfiSupport.makeStr(arena, value);
            int err = (int) ResponseFfi.MH_aegon_res_header.invokeExact(resPtr, nameSeg, valSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to set header: " + t.getMessage(), t);
        }
    }

    /**
     * Sets the response body as plain text with {@code Content-Type: text/plain}.
     *
     * @param text plain text content
     * @return {@code this} response builder
     */
    public Response text(String text) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment strSeg = FfiSupport.makeStr(arena, text);
            int err = (int) ResponseFfi.MH_aegon_res_text.invokeExact(resPtr, strSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to set text body: " + t.getMessage(), t);
        }
    }

    /**
     * Sets a pre-serialized JSON string as the response body with {@code Content-Type: application/json}.
     *
     * @param json raw JSON string
     * @return {@code this} response builder
     */
    public Response json(String json) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment strSeg = FfiSupport.makeStr(arena, json);
            int err = (int) ResponseFfi.MH_aegon_res_json.invokeExact(resPtr, strSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to set JSON body: " + t.getMessage(), t);
        }
    }

    /**
     * Serializes any Java object, record, list, or map to JSON using Fastjson2 and sets {@code Content-Type: application/json}.
     *
     * @param object Java record or POJO to serialize
     * @return {@code this} response builder
     */
    public Response json(Object object) {
        if (object instanceof String s) {
            return json(s);
        }
        byte[] jsonBytes = Json.encodeBytes(object);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment strSeg = FfiSupport.makeStr(arena, jsonBytes);
            int err = (int) ResponseFfi.MH_aegon_res_json.invokeExact(resPtr, strSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to serialize and set JSON: " + t.getMessage(), t);
        }
    }

    /**
     * Sets HTML content as the response body with {@code Content-Type: text/html; charset=utf-8}.
     *
     * @param html HTML markup
     * @return {@code this} response builder
     */
    public Response html(String html) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment strSeg = FfiSupport.makeStr(arena, html);
            int err = (int) ResponseFfi.MH_aegon_res_html.invokeExact(resPtr, strSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to set HTML body: " + t.getMessage(), t);
        }
    }

    /**
     * Sets arbitrary string content as the body without altering the {@code Content-Type} header.
     *
     * @param data body string
     * @return {@code this} response builder
     */
    public Response body(String data) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment strSeg = FfiSupport.makeStr(arena, data);
            int err = (int) ResponseFfi.MH_aegon_res_body.invokeExact(resPtr, strSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to set body: " + t.getMessage(), t);
        }
    }

    /**
     * Sets binary bytes as the body without altering {@code Content-Type}.
     *
     * @param data raw byte array
     * @return {@code this} response builder
     */
    public Response body(byte[] data) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment strSeg = FfiSupport.makeStr(arena, data);
            int err = (int) ResponseFfi.MH_aegon_res_body.invokeExact(resPtr, strSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to set binary body: " + t.getMessage(), t);
        }
    }

    /**
     * Sets a zero-copy {@link MemorySegment} as the response body.
     *
     * <p>Warning: the referenced off-heap memory segment MUST outlive the HTTP response transmission!
     *
     * @param segment off-heap memory segment
     * @return {@code this} response builder
     */
    public Response bodySegment(MemorySegment segment) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment strSeg = FfiSupport.makeStr(arena, segment);
            ResponseFfi.MH_aegon_res_body_static.invokeExact(resPtr, strSeg);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to set body view: " + t.getMessage(), t);
        }
    }

    /**
     * Sets an HTTP cookie using {@link CookieOptions}.
     *
     * @param opts configured cookie options
     * @return {@code this} response builder
     */
    public Response cookie(CookieOptions opts) {
        if (opts == null) return this;
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment optsSeg = arena.allocate(ResponseFfi.COOKIE_OPTIONS_LAYOUT);
            ResponseFfi.MH_aegon_cookie_options_init.invokeExact(optsSeg);

            optsSeg.set(ValueLayout.JAVA_LONG, ResponseFfi.COOKIE_OPTIONS_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("struct_size")), ResponseFfi.COOKIE_OPTIONS_LAYOUT.byteSize());

            MemorySegment nameSeg = FfiSupport.makeStr(arena, opts.name());
            optsSeg.asSlice(ResponseFfi.COOKIE_OPTIONS_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("name")), FfiSupport.STR_LAYOUT.byteSize()).copyFrom(nameSeg);

            MemorySegment valSeg = FfiSupport.makeStr(arena, opts.value());
            optsSeg.asSlice(ResponseFfi.COOKIE_OPTIONS_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("value")), FfiSupport.STR_LAYOUT.byteSize()).copyFrom(valSeg);

            MemorySegment pathSeg = FfiSupport.makeStr(arena, opts.path());
            optsSeg.asSlice(ResponseFfi.COOKIE_OPTIONS_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("path")), FfiSupport.STR_LAYOUT.byteSize()).copyFrom(pathSeg);

            MemorySegment domainSeg = FfiSupport.makeStr(arena, opts.domain());
            optsSeg.asSlice(ResponseFfi.COOKIE_OPTIONS_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("domain")), FfiSupport.STR_LAYOUT.byteSize()).copyFrom(domainSeg);

            optsSeg.set(ValueLayout.JAVA_LONG, ResponseFfi.COOKIE_OPTIONS_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("max_age_seconds")), opts.maxAgeSeconds());
            optsSeg.set(ValueLayout.JAVA_INT, ResponseFfi.COOKIE_OPTIONS_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("same_site")), opts.sameSite().nativeCode());
            optsSeg.set(ValueLayout.JAVA_BOOLEAN, ResponseFfi.COOKIE_OPTIONS_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("http_only")), opts.httpOnly());
            optsSeg.set(ValueLayout.JAVA_BOOLEAN, ResponseFfi.COOKIE_OPTIONS_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("secure")), opts.secure());
            optsSeg.set(ValueLayout.JAVA_BOOLEAN, ResponseFfi.COOKIE_OPTIONS_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("partitioned")), opts.partitioned());

            int err = (int) ResponseFfi.MH_aegon_res_set_cookie.invokeExact(resPtr, optsSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to set cookie: " + t.getMessage(), t);
        }
    }

    /**
     * Sets a simple session cookie with name and value.
     *
     * @param name cookie name
     * @param value cookie value
     * @return {@code this} response builder
     */
    public Response cookie(String name, String value) {
        return cookie(CookieOptions.of(name, value));
    }

    /**
     * Clears an HTTP cookie immediately on the client ({@code Max-Age=0}) with default path "/".
     *
     * @param name cookie name
     * @return {@code this} response builder
     */
    public Response clearCookie(String name) {
        return clearCookie(name, "/");
    }

    /**
     * Clears an HTTP cookie immediately on the client ({@code Max-Age=0}) with the specified path.
     *
     * @param name cookie name
     * @param path cookie path
     * @return {@code this} response builder
     */
    public Response clearCookie(String name, String path) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nameSeg = FfiSupport.makeStr(arena, name);
            MemorySegment pathSeg = FfiSupport.makeStr(arena, path);
            int err = (int) ResponseFfi.MH_aegon_res_clear_cookie.invokeExact(resPtr, nameSeg, pathSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to clear cookie: " + t.getMessage(), t);
        }
    }

    /**
     * Configures the response to transmit a file using kernel zero-copy sendfile/splice.
     *
     * @param path filesystem path to file
     * @return {@code this} response builder
     */
    public Response file(String path) {
        return file(path, null);
    }

    /**
     * Configures the response to transmit a file with an explicit MIME type.
     *
     * @param path filesystem path to file
     * @param mime MIME type (e.g. "image/png"), or null to infer from extension
     * @return {@code this} response builder
     */
    public Response file(String path, String mime) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment pathSeg = arena.allocateFrom(path);
            MemorySegment mimeSeg = FfiSupport.makeStr(arena, mime);
            int err = (int) ResponseFfi.MH_aegon_res_file.invokeExact(resPtr, pathSeg, mimeSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to send file: " + t.getMessage(), t);
        }
    }

    /**
     * Emits a redirect response with 3xx status and {@code Location} header.
     *
     * @param status redirect status code (e.g. 301, 302, 307, 308)
     * @param location destination URL
     * @return {@code this} response builder
     */
    public Response redirect(StatusCode status, String location) {
        return redirect(status.code(), location);
    }

    /**
     * Emits a redirect response with numeric status and {@code Location} header.
     *
     * @param status 3xx status code
     * @param location destination URL
     * @return {@code this} response builder
     */
    public Response redirect(int status, String location) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment locSeg = FfiSupport.makeStr(arena, location);
            int err = (int) ResponseFfi.MH_aegon_res_redirect.invokeExact(resPtr, (short) status, locSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to redirect: " + t.getMessage(), t);
        }
    }

    /**
     * Sets an HTTP/2 or HTTP/3 trailing header.
     *
     * @param name trailer header name
     * @param value trailer header value
     * @return {@code this} response builder
     */
    public Response trailer(String name, String value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nameSeg = FfiSupport.makeStr(arena, name);
            MemorySegment valSeg = FfiSupport.makeStr(arena, value);
            int err = (int) ResponseFfi.MH_aegon_res_trailer.invokeExact(resPtr, nameSeg, valSeg);
            FfiSupport.checkErr(err);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to set trailer: " + t.getMessage(), t);
        }
    }

    /**
     * Specifies whether to send only trailers (e.g. gRPC trailers-only responses).
     *
     * @param on true for trailers-only
     * @return {@code this} response builder
     */
    public Response trailersOnly(boolean on) {
        try {
            ResponseFfi.MH_aegon_res_trailers_only.invokeExact(resPtr, on);
            return this;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to set trailers only: " + t.getMessage(), t);
        }
    }
}
