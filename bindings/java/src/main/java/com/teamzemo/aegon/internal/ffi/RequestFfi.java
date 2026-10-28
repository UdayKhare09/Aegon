package com.teamzemo.aegon.internal.ffi;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * Native downcalls for Aegon HTTP request introspection ({@code aegon_req_*}) from {@code request.h}.
 */
public final class RequestFfi {

    public static final MethodHandle MH_aegon_req_method = FfiSupport.downcall("aegon_req_method",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_req_version = FfiSupport.downcall("aegon_req_version",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_req_path = FfiSupport.downcall("aegon_req_path",
            FunctionDescriptor.of(FfiSupport.STR_LAYOUT, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_req_query_string = FfiSupport.downcall("aegon_req_query_string",
            FunctionDescriptor.of(FfiSupport.STR_LAYOUT, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_req_body = FfiSupport.downcall("aegon_req_body",
            FunctionDescriptor.of(FfiSupport.STR_LAYOUT, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_req_header = FfiSupport.downcall("aegon_req_header",
            FunctionDescriptor.of(FfiSupport.STR_LAYOUT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_req_has_header = FfiSupport.downcall("aegon_req_has_header",
            FunctionDescriptor.of(ValueLayout.JAVA_BOOLEAN, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_req_header_count = FfiSupport.downcall("aegon_req_header_count",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_req_header_at = FfiSupport.downcall("aegon_req_header_at",
            FunctionDescriptor.of(ValueLayout.JAVA_BOOLEAN, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_req_param = FfiSupport.downcall("aegon_req_param",
            FunctionDescriptor.of(FfiSupport.STR_LAYOUT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_req_param_count = FfiSupport.downcall("aegon_req_param_count",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_req_param_at = FfiSupport.downcall("aegon_req_param_at",
            FunctionDescriptor.of(ValueLayout.JAVA_BOOLEAN, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_req_query = FfiSupport.downcall("aegon_req_query",
            FunctionDescriptor.of(FfiSupport.STR_LAYOUT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_req_cookie = FfiSupport.downcall("aegon_req_cookie",
            FunctionDescriptor.of(FfiSupport.STR_LAYOUT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_req_is_grpc = FfiSupport.downcall("aegon_req_is_grpc",
            FunctionDescriptor.of(ValueLayout.JAVA_BOOLEAN, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_req_is_websocket = FfiSupport.downcall("aegon_req_is_websocket",
            FunctionDescriptor.of(ValueLayout.JAVA_BOOLEAN, ValueLayout.ADDRESS));

    private RequestFfi() {}
}

