package com.teamzemo.aegon.http;

import com.teamzemo.aegon.internal.ffi.CoreFfi;
import com.teamzemo.aegon.internal.ffi.FfiSupport;

import java.lang.foreign.MemorySegment;

/**
 * HTTP Protocol Version mirroring Aegon C++ {@code aegon::http::HttpVersion}.
 */
public enum HttpVersion {
    HTTP_1_0(0, "HTTP/1.0"),
    HTTP_1_1(1, "HTTP/1.1"),
    HTTP_2(2, "HTTP/2.0"),
    HTTP_3(3, "HTTP/3.0");

    private final int nativeCode;
    private final String defaultString;

    HttpVersion(int nativeCode, String defaultString) {
        this.nativeCode = nativeCode;
        this.defaultString = defaultString;
    }

    /**
     * Returns the C ABI integer code for this HTTP version.
     *
     * @return integer enum code
     */
    public int nativeCode() {
        return nativeCode;
    }

    /**
     * Returns the protocol version string representation (e.g. "HTTP/1.1", "HTTP/2.0", "HTTP/3.0").
     *
     * @return protocol string
     */
    public String asString() {
        try {
            MemorySegment ptr = (MemorySegment) CoreFfi.MH_aegon_http_version_cstr.invokeExact(nativeCode);
            String str = FfiSupport.readCString(ptr);
            return (str != null && !str.isEmpty()) ? str : defaultString;
        } catch (Throwable t) {
            return defaultString;
        }
    }

    /**
     * Resolves an integer native code to its corresponding {@link HttpVersion} enum constant.
     *
     * @param code integer native code
     * @return {@link HttpVersion} constant
     */
    public static HttpVersion fromNativeCode(int code) {
        for (HttpVersion v : values()) {
            if (v.nativeCode == code) {
                return v;
            }
        }
        return HTTP_1_1;
    }
}

