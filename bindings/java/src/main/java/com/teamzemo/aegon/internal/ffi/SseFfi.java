package com.teamzemo.aegon.internal.ffi;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * Native downcalls and upcall descriptors for Server-Sent Events (SSE) from {@code sse.h}.
 */
public final class SseFfi {

    /**
     * SSE on_close upcall: {@code void (*)(void* userdata)}
     */
    public static final FunctionDescriptor SSE_CLOSE_UPCALL_DESC = FunctionDescriptor.ofVoid(
            ValueLayout.ADDRESS
    );

    public static final MethodHandle MH_aegon_sse_create = FfiSupport.downcall("aegon_sse_create",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_sse_send_event = FfiSupport.downcall("aegon_sse_send_event",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_sse_send_data = FfiSupport.downcall("aegon_sse_send_data",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_sse_send_comment = FfiSupport.downcall("aegon_sse_send_comment",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_sse_send_retry = FfiSupport.downcall("aegon_sse_send_retry",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));

    public static final MethodHandle MH_aegon_sse_close = FfiSupport.downcall("aegon_sse_close",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_sse_is_open = FfiSupport.downcall("aegon_sse_is_open",
            FunctionDescriptor.of(ValueLayout.JAVA_BOOLEAN, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_sse_destroy = FfiSupport.downcall("aegon_sse_destroy",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    private SseFfi() {}
}

