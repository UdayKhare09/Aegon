package com.teamzemo.aegon.internal.ffi;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * Native downcalls for Aegon global logging subsystem from {@code log.h}.
 */
public final class LogFfi {

    public static final MethodHandle MH_aegon_log_init = FfiSupport.downcall("aegon_log_init",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT, ValueLayout.JAVA_BOOLEAN));

    public static final MethodHandle MH_aegon_log_set_level = FfiSupport.downcall("aegon_log_set_level",
            FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT));

    public static final MethodHandle MH_aegon_log_level = FfiSupport.downcall("aegon_log_level",
            FunctionDescriptor.of(ValueLayout.JAVA_INT));

    public static final MethodHandle MH_aegon_log_add_file_sink = FfiSupport.downcall("aegon_log_add_file_sink",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_log_clear_sinks = FfiSupport.downcall("aegon_log_clear_sinks",
            FunctionDescriptor.ofVoid());

    public static final MethodHandle MH_aegon_log_write = FfiSupport.downcall("aegon_log_write",
            FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_log_flush = FfiSupport.downcall("aegon_log_flush",
            FunctionDescriptor.ofVoid());

    public static final MethodHandle MH_aegon_log_shutdown = FfiSupport.downcall("aegon_log_shutdown",
            FunctionDescriptor.ofVoid());

    private LogFfi() {}
}

