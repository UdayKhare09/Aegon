package com.teamzemo.aegon.internal.ffi;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * Native downcalls for Aegon HTTP Context lifecycle operations ({@code aegon_ctx_*}) from {@code context.h}.
 */
public final class ContextFfi {

    public static final MethodHandle MH_aegon_ctx_req = FfiSupport.downcall("aegon_ctx_req",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_ctx_res = FfiSupport.downcall("aegon_ctx_res",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_ctx_app_data = FfiSupport.downcall("aegon_ctx_app_data",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_ctx_local_set = FfiSupport.downcall("aegon_ctx_local_set",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_ctx_local_get = FfiSupport.downcall("aegon_ctx_local_get",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_ctx_problem = FfiSupport.downcall("aegon_ctx_problem",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT, FfiSupport.STR_LAYOUT, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_ctx_send_file = FfiSupport.downcall("aegon_ctx_send_file",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    private ContextFfi() {}
}

