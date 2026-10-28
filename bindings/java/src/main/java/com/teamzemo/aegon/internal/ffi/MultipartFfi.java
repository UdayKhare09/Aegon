package com.teamzemo.aegon.internal.ffi;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.StructLayout;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * Native downcalls and struct layouts for multipart/form-data parsing from {@code multipart.h}.
 */
public final class MultipartFfi {

    /**
     * Struct layout for {@code aegon_multipart_file_info_t}:
     * <pre>{@code
     * typedef struct aegon_multipart_file_info {
     *     aegon_str_t name;
     *     aegon_str_t filename;
     *     aegon_str_t content_type;
     *     aegon_str_t data;
     * } aegon_multipart_file_info_t;
     * }</pre>
     */
    public static final StructLayout MULTIPART_FILE_INFO_LAYOUT = MemoryLayout.structLayout(
            FfiSupport.STR_LAYOUT.withName("name"),
            FfiSupport.STR_LAYOUT.withName("filename"),
            FfiSupport.STR_LAYOUT.withName("content_type"),
            FfiSupport.STR_LAYOUT.withName("data")
    ).withName("aegon_multipart_file_info_t");

    public static final MethodHandle MH_aegon_multipart_parse = FfiSupport.downcall("aegon_multipart_parse",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_multipart_destroy = FfiSupport.downcall("aegon_multipart_destroy",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_multipart_field = FfiSupport.downcall("aegon_multipart_field",
            FunctionDescriptor.of(FfiSupport.STR_LAYOUT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_multipart_field_count = FfiSupport.downcall("aegon_multipart_field_count",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_multipart_field_at = FfiSupport.downcall("aegon_multipart_field_at",
            FunctionDescriptor.of(ValueLayout.JAVA_BOOLEAN, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_multipart_file = FfiSupport.downcall("aegon_multipart_file",
            FunctionDescriptor.of(ValueLayout.JAVA_BOOLEAN, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_multipart_file_count = FfiSupport.downcall("aegon_multipart_file_count",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_multipart_file_at = FfiSupport.downcall("aegon_multipart_file_at",
            FunctionDescriptor.of(ValueLayout.JAVA_BOOLEAN, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));

    private MultipartFfi() {}
}

