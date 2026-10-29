package com.teamzemo.aegon.internal.ffi;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * Native downcalls for Aegon built-in middleware extensions:
 * CORS, Compression, Security Headers, Access Logging, and Static Files.
 */
public final class ExtensionsFfi {

    /* --- CORS (cors.h) --- */
    public static final MethodHandle MH_aegon_cors_config_init = FfiSupport.downcall("aegon_cors_config_init",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_router_use_cors = FfiSupport.downcall("aegon_router_use_cors",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_group_use_cors = FfiSupport.downcall("aegon_group_use_cors",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    /* --- Compression (compress.h) --- */
    public static final MethodHandle MH_aegon_compress_config_init = FfiSupport.downcall("aegon_compress_config_init",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_router_use_compress = FfiSupport.downcall("aegon_router_use_compress",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_group_use_compress = FfiSupport.downcall("aegon_group_use_compress",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    /* --- Security Headers (security.h) --- */
    public static final MethodHandle MH_aegon_security_config_init = FfiSupport.downcall("aegon_security_config_init",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_router_use_security_headers = FfiSupport.downcall("aegon_router_use_security_headers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_group_use_security_headers = FfiSupport.downcall("aegon_group_use_security_headers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    /* --- Static Files (static_files.h) --- */
    public static final MethodHandle MH_aegon_static_files_config_init = FfiSupport.downcall("aegon_static_files_config_init",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_router_static_files = FfiSupport.downcall("aegon_router_static_files",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_group_static_files = FfiSupport.downcall("aegon_group_static_files",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private ExtensionsFfi() {}
}

