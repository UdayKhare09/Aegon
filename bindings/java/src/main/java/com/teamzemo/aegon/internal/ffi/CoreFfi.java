package com.teamzemo.aegon.internal.ffi;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * Native downcalls for Aegon core ABI versioning, status codes, methods, and memory management.
 */
public final class CoreFfi {

    public static final MethodHandle MH_aegon_version = FfiSupport.downcall("aegon_version",
            FunctionDescriptor.of(ValueLayout.JAVA_INT));

    public static final MethodHandle MH_aegon_version_string = FfiSupport.downcall("aegon_version_string",
            FunctionDescriptor.of(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_abi_version = FfiSupport.downcall("aegon_abi_version",
            FunctionDescriptor.of(ValueLayout.JAVA_INT));

    public static final MethodHandle MH_aegon_status_phrase = FfiSupport.downcall("aegon_status_phrase",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));

    public static final MethodHandle MH_aegon_method_cstr = FfiSupport.downcall("aegon_method_cstr",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.JAVA_INT));

    public static final MethodHandle MH_aegon_method_from_str = FfiSupport.downcall("aegon_method_from_str",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_http_version_cstr = FfiSupport.downcall("aegon_http_version_cstr",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.JAVA_INT));

    public static final MethodHandle MH_aegon_err_str = FfiSupport.downcall("aegon_err_str",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.JAVA_INT));

    public static final MethodHandle MH_aegon_last_error = FfiSupport.downcall("aegon_last_error",
            FunctionDescriptor.of(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_last_error_clear = FfiSupport.downcall("aegon_last_error_clear",
            FunctionDescriptor.ofVoid());

    public static final MethodHandle MH_aegon_str = FfiSupport.downcall("aegon_str",
            FunctionDescriptor.of(FfiSupport.STR_LAYOUT, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_str_dup = FfiSupport.downcall("aegon_str_dup",
            FunctionDescriptor.of(FfiSupport.STR_LAYOUT, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_mem_free = FfiSupport.downcall("aegon_mem_free",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_url_decode = FfiSupport.downcall("aegon_url_decode",
            FunctionDescriptor.of(FfiSupport.STR_LAYOUT, FfiSupport.STR_LAYOUT));

    private CoreFfi() {}
}

