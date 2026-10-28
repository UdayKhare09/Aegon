package com.teamzemo.aegon.internal.ffi;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * Native downcalls and upcall descriptors for Aegon onion-style HTTP middleware chain from {@code middleware.h}.
 */
public final class MiddlewareFfi {

    /**
     * Middleware upcall signature: {@code void (*)(aegon_ctx_t* ctx, aegon_next_t* next, void* userdata)}
     */
    public static final FunctionDescriptor MIDDLEWARE_UPCALL_DESC = FunctionDescriptor.ofVoid(
            ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS
    );

    public static final java.lang.foreign.StructLayout MIDDLEWARE_SLOT_LAYOUT = java.lang.foreign.MemoryLayout.structLayout(
            ValueLayout.ADDRESS.withName("fn"),
            ValueLayout.ADDRESS.withName("userdata"),
            ValueLayout.ADDRESS.withName("cleanup")
    );

    public static final MethodHandle MH_aegon_next_call = FfiSupport.downcall("aegon_next_call",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_router_use = FfiSupport.downcall("aegon_router_use",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_group_use = FfiSupport.downcall("aegon_group_use",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private MiddlewareFfi() {}
}

