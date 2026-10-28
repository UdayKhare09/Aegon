package com.teamzemo.aegon.internal.ffi;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * Native downcalls and upcall descriptors for Aegon Router and RouteGroup operations from {@code router.h}.
 */
public final class RouterFfi {

    /* Upcall Descriptors */
    public static final FunctionDescriptor HANDLER_UPCALL_DESC = FunctionDescriptor.ofVoid(
            ValueLayout.ADDRESS, ValueLayout.ADDRESS
    );

    public static final FunctionDescriptor ERROR_HANDLER_UPCALL_DESC = FunctionDescriptor.ofVoid(
            ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT, ValueLayout.ADDRESS
    );

    public static final FunctionDescriptor CLEANUP_UPCALL_DESC = FunctionDescriptor.ofVoid(
            ValueLayout.ADDRESS
    );

    /* Router Downcalls */
    public static final MethodHandle MH_aegon_router_create = FfiSupport.downcall("aegon_router_create",
            FunctionDescriptor.of(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_router_destroy = FfiSupport.downcall("aegon_router_destroy",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_router_add = FfiSupport.downcall("aegon_router_add",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_router_add_async = FfiSupport.downcall("aegon_router_add_async",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_router_on_error = FfiSupport.downcall("aegon_router_on_error",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_router_on_not_found = FfiSupport.downcall("aegon_router_on_not_found",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_router_on_method_not_allowed = FfiSupport.downcall("aegon_router_on_method_not_allowed",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_router_group = FfiSupport.downcall("aegon_router_group",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    /* RouteGroup Downcalls */
    public static final MethodHandle MH_aegon_group_group = FfiSupport.downcall("aegon_group_group",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_group_destroy = FfiSupport.downcall("aegon_group_destroy",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_group_add = FfiSupport.downcall("aegon_group_add",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_group_add_async = FfiSupport.downcall("aegon_group_add_async",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private RouterFfi() {}
}

