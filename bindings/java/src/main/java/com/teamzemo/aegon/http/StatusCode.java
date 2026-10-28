package com.teamzemo.aegon.http;

import com.teamzemo.aegon.internal.ffi.CoreFfi;
import com.teamzemo.aegon.internal.ffi.FfiSupport;

import java.lang.foreign.MemorySegment;

/**
 * Standard HTTP Status Codes as defined in RFC 9110 and Aegon C++ {@code aegon::http::StatusCode}.
 */
public enum StatusCode {

    CONTINUE(100, "Continue"),
    SWITCHING_PROTOCOLS(101, "Switching Protocols"),

    OK(200, "OK"),
    CREATED(201, "Created"),
    ACCEPTED(202, "Accepted"),
    NO_CONTENT(204, "No Content"),
    PARTIAL_CONTENT(206, "Partial Content"),

    MOVED_PERMANENTLY(301, "Moved Permanently"),
    FOUND(302, "Found"),
    SEE_OTHER(303, "See Other"),
    NOT_MODIFIED(304, "Not Modified"),
    TEMPORARY_REDIRECT(307, "Temporary Redirect"),
    PERMANENT_REDIRECT(308, "Permanent Redirect"),

    BAD_REQUEST(400, "Bad Request"),
    UNAUTHORIZED(401, "Unauthorized"),
    FORBIDDEN(403, "Forbidden"),
    NOT_FOUND(404, "Not Found"),
    METHOD_NOT_ALLOWED(405, "Method Not Allowed"),
    REQUEST_TIMEOUT(408, "Request Timeout"),
    CONFLICT(409, "Conflict"),
    PAYLOAD_TOO_LARGE(413, "Payload Too Large"),
    URI_TOO_LONG(414, "URI Too Long"),
    UNPROCESSABLE_ENTITY(422, "Unprocessable Entity"),
    UPGRADE_REQUIRED(426, "Upgrade Required"),
    TOO_MANY_REQUESTS(429, "Too Many Requests"),

    INTERNAL_SERVER_ERROR(500, "Internal Server Error"),
    NOT_IMPLEMENTED(501, "Not Implemented"),
    BAD_GATEWAY(502, "Bad Gateway"),
    SERVICE_UNAVAILABLE(503, "Service Unavailable"),
    GATEWAY_TIMEOUT(504, "Gateway Timeout");

    private final int code;
    private final String defaultPhrase;

    StatusCode(int code, String defaultPhrase) {
        this.code = code;
        this.defaultPhrase = defaultPhrase;
    }

    /**
     * Returns the 3-digit numeric HTTP status code.
     *
     * @return numeric status code
     */
    public int code() {
        return code;
    }

    /**
     * Returns the standard RFC 9110 reason phrase for this status code.
     *
     * @return status reason phrase
     */
    public String phrase() {
        try {
            MemorySegment ptr = (MemorySegment) CoreFfi.MH_aegon_status_phrase.invokeExact((short) code);
            String p = FfiSupport.readCString(ptr);
            return (p != null && !p.isEmpty()) ? p : defaultPhrase;
        } catch (Throwable t) {
            return defaultPhrase;
        }
    }

    /**
     * Resolves a numeric status code to a known {@link StatusCode} enum constant.
     *
     * @param code integer status code
     * @return matching {@link StatusCode}, or {@code null} if custom / non-standard
     */
    public static StatusCode fromCode(int code) {
        for (StatusCode s : values()) {
            if (s.code == code) {
                return s;
            }
        }
        return null;
    }
}

