package com.teamzemo.aegon.internal.ffi;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * Native downcalls for Aegon HTTP Server lifecycle operations from {@code server.h}.
 */
public final class ServerFfi {

    public static final MethodHandle MH_aegon_server_config_init = FfiSupport.downcall("aegon_server_config_init",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_server_create = FfiSupport.downcall("aegon_server_create",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_server_destroy = FfiSupport.downcall("aegon_server_destroy",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_server_set_config = FfiSupport.downcall("aegon_server_set_config",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_server_listen = FfiSupport.downcall("aegon_server_listen",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_server_listen_tls = FfiSupport.downcall("aegon_server_listen_tls",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_server_enable_tls = FfiSupport.downcall("aegon_server_enable_tls",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_server_set_app_data = FfiSupport.downcall("aegon_server_set_app_data",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_server_app_data = FfiSupport.downcall("aegon_server_app_data",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_server_run = FfiSupport.downcall("aegon_server_run",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));

    public static final MethodHandle MH_aegon_server_run_async = FfiSupport.downcall("aegon_server_run_async",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));

    public static final MethodHandle MH_aegon_server_stop = FfiSupport.downcall("aegon_server_stop",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_server_join = FfiSupport.downcall("aegon_server_join",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_server_is_running = FfiSupport.downcall("aegon_server_is_running",
            FunctionDescriptor.of(ValueLayout.JAVA_BOOLEAN, ValueLayout.ADDRESS));

    public static final FunctionDescriptor LIFECYCLE_UPCALL_DESC = FunctionDescriptor.ofVoid(
            ValueLayout.ADDRESS, ValueLayout.ADDRESS
    );

    public static final FunctionDescriptor WORKER_UPCALL_DESC = FunctionDescriptor.ofVoid(
            ValueLayout.ADDRESS
    );

    public static final MethodHandle MH_aegon_server_on_start = FfiSupport.downcall("aegon_server_on_start",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_server_on_stop = FfiSupport.downcall("aegon_server_on_stop",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_server_spawn_worker = FfiSupport.downcall("aegon_server_spawn_worker",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private ServerFfi() {}
}

