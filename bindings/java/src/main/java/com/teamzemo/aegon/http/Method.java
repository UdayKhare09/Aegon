package com.teamzemo.aegon.http;

import com.teamzemo.aegon.internal.ffi.CoreFfi;
import com.teamzemo.aegon.internal.ffi.FfiSupport;

import java.lang.foreign.MemorySegment;

/**
 * Standard HTTP Request Methods mirroring Aegon C++ {@code aegon::http::Method}.
 */
public enum Method {
    GET(0),
    POST(1),
    PUT(2),
    DELETE(3),
    PATCH(4),
    HEAD(5),
    OPTIONS(6),
    CONNECT(7),
    TRACE(8),
    UNKNOWN(9);

    private final int nativeCode;

    Method(int nativeCode) {
        this.nativeCode = nativeCode;
    }

    /**
     * Returns the underlying C ABI integer representation of this HTTP method.
     *
     * @return integer enum code
     */
    public int nativeCode() {
        return nativeCode;
    }

    /**
     * Returns the canonical uppercase method name string (e.g. "GET", "POST").
     *
     * @return HTTP method string
     */
    public String asString() {
        try {
            MemorySegment ptr = (MemorySegment) CoreFfi.MH_aegon_method_cstr.invokeExact(nativeCode);
            String str = FfiSupport.readCString(ptr);
            return (str != null && !str.isEmpty()) ? str : name();
        } catch (Throwable t) {
            return name();
        }
    }

    /**
     * Resolves a C ABI native code to its corresponding {@link Method} enum constant.
     *
     * @param code integer native code
     * @return {@link Method} constant
     */
    public static Method fromNativeCode(int code) {
        for (Method m : values()) {
            if (m.nativeCode == code) {
                return m;
            }
        }
        return UNKNOWN;
    }
}

