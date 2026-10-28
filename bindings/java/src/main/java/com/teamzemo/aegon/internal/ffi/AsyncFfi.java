package com.teamzemo.aegon.internal.ffi;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * Native downcalls for Aegon asynchronous coroutine bridging ({@code aegon_async_*}) from {@code async.h}.
 */
public final class AsyncFfi {

    public static final MethodHandle MH_aegon_async_create = FfiSupport.downcall("aegon_async_create",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_async_complete = FfiSupport.downcall("aegon_async_complete",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    private AsyncFfi() {}
}

