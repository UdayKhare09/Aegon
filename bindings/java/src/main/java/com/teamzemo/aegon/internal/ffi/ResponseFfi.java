package com.teamzemo.aegon.internal.ffi;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.StructLayout;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * Native downcalls and struct layouts for Aegon HTTP response building ({@code aegon_res_*}) from {@code response.h}.
 */
public final class ResponseFfi {

    /**
     * Struct layout of {@code aegon_cookie_options_t}:
     * <pre>{@code
     * typedef struct aegon_cookie_options {
     *     size_t           struct_size;
     *     aegon_str_t      name;
     *     aegon_str_t      value;
     *     aegon_str_t      path;
     *     aegon_str_t      domain;
     *     int64_t          max_age_seconds;
     *     aegon_samesite_t same_site;
     *     bool             http_only;
     *     bool             secure;
     *     bool             partitioned;
     * } aegon_cookie_options_t;
     * }</pre>
     */
    public static final StructLayout COOKIE_OPTIONS_LAYOUT = MemoryLayout.structLayout(
            ValueLayout.JAVA_LONG.withName("struct_size"),
            FfiSupport.STR_LAYOUT.withName("name"),
            FfiSupport.STR_LAYOUT.withName("value"),
            FfiSupport.STR_LAYOUT.withName("path"),
            FfiSupport.STR_LAYOUT.withName("domain"),
            ValueLayout.JAVA_LONG.withName("max_age_seconds"),
            ValueLayout.JAVA_INT.withName("same_site"),
            ValueLayout.JAVA_BOOLEAN.withName("http_only"),
            ValueLayout.JAVA_BOOLEAN.withName("secure"),
            ValueLayout.JAVA_BOOLEAN.withName("partitioned"),
            MemoryLayout.paddingLayout(1)
    ).withName("aegon_cookie_options_t");

    public static final MethodHandle MH_aegon_res_status = FfiSupport.downcall("aegon_res_status",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));

    public static final MethodHandle MH_aegon_res_header = FfiSupport.downcall("aegon_res_header",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_res_header_static = FfiSupport.downcall("aegon_res_header_static",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_res_body = FfiSupport.downcall("aegon_res_body",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_res_body_static = FfiSupport.downcall("aegon_res_body_static",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_res_text = FfiSupport.downcall("aegon_res_text",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_res_json = FfiSupport.downcall("aegon_res_json",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_res_html = FfiSupport.downcall("aegon_res_html",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_cookie_options_init = FfiSupport.downcall("aegon_cookie_options_init",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_res_set_cookie = FfiSupport.downcall("aegon_res_set_cookie",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_res_clear_cookie = FfiSupport.downcall("aegon_res_clear_cookie",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_res_file = FfiSupport.downcall("aegon_res_file",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_res_redirect = FfiSupport.downcall("aegon_res_redirect",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_mime_from_extension = FfiSupport.downcall("aegon_mime_from_extension",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_res_trailer = FfiSupport.downcall("aegon_res_trailer",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_res_trailers_only = FfiSupport.downcall("aegon_res_trailers_only",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.JAVA_BOOLEAN));

    private ResponseFfi() {}
}

