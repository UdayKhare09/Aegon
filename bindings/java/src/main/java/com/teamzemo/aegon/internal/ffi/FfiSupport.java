package com.teamzemo.aegon.internal.ffi;

import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;
import java.nio.charset.StandardCharsets;

/**
 * Common FFM utilities, downcall resolvers, and off-heap memory helpers for the Aegon native bridge.
 */
public final class FfiSupport {

    public static final Linker LINKER = Linker.nativeLinker();
    public static final SymbolLookup SYMBOLS = NativeLibraryLoader.lookup();

    /**
     * Struct layout for C {@code aegon_str_t}:
     * <pre>{@code
     * struct aegon_str {
     *     const char* data;
     *     size_t      len;
     * };
     * }</pre>
     */
    public static final StructLayout STR_LAYOUT = MemoryLayout.structLayout(
            ValueLayout.ADDRESS.withName("data"),
            ValueLayout.JAVA_LONG.withName("len")
    ).withName("aegon_str_t");

    public static final long STR_DATA_OFFSET = STR_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("data"));
    public static final long STR_LEN_OFFSET = STR_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("len"));

    private FfiSupport() {}

    /**
     * Resolves a C native function symbol and constructs an optimized JIT downcall handle.
     *
     * @param name C function symbol name
     * @param desc FFM function descriptor
     * @return direct downcall {@link MethodHandle}
     */
    public static MethodHandle downcall(String name, FunctionDescriptor desc) {
        return LINKER.downcallHandle(SYMBOLS.find(name).orElseThrow(() ->
                new UnsatisfiedLinkError("Aegon C ABI symbol not found: " + name)), desc);
    }

    /**
     * Allocates an {@code aegon_str_t} slice pointing to the UTF-8 bytes of {@code s}.
     *
     * @param arena memory arena to allocate within
     * @param s Java string (may be null)
     * @return {@link MemorySegment} formatted according to {@link #STR_LAYOUT}
     */
    public static MemorySegment makeStr(Arena arena, String s) {
        MemorySegment structSeg = arena.allocate(STR_LAYOUT);
        if (s == null) {
            structSeg.set(ValueLayout.ADDRESS, STR_DATA_OFFSET, MemorySegment.NULL);
            structSeg.set(ValueLayout.JAVA_LONG, STR_LEN_OFFSET, 0L);
            return structSeg;
        }
        byte[] utf8 = s.getBytes(StandardCharsets.UTF_8);
        if (utf8.length == 0) {
            structSeg.set(ValueLayout.ADDRESS, STR_DATA_OFFSET, MemorySegment.NULL);
            structSeg.set(ValueLayout.JAVA_LONG, STR_LEN_OFFSET, 0L);
            return structSeg;
        }
        MemorySegment dataSeg = arena.allocate(utf8.length);
        MemorySegment.copy(utf8, 0, dataSeg, ValueLayout.JAVA_BYTE, 0, utf8.length);
        structSeg.set(ValueLayout.ADDRESS, STR_DATA_OFFSET, dataSeg);
        structSeg.set(ValueLayout.JAVA_LONG, STR_LEN_OFFSET, (long) utf8.length);
        return structSeg;
    }

    /**
     * Allocates an {@code aegon_str_t} slice pointing to raw bytes in {@code data}.
     *
     * @param arena memory arena
     * @param data byte array
     * @return {@link MemorySegment} formatted according to {@link #STR_LAYOUT}
     */
    public static MemorySegment makeStr(Arena arena, byte[] data) {
        MemorySegment structSeg = arena.allocate(STR_LAYOUT);
        if (data == null || data.length == 0) {
            structSeg.set(ValueLayout.ADDRESS, STR_DATA_OFFSET, MemorySegment.NULL);
            structSeg.set(ValueLayout.JAVA_LONG, STR_LEN_OFFSET, 0L);
            return structSeg;
        }
        MemorySegment dataSeg = arena.allocate(data.length);
        MemorySegment.copy(data, 0, dataSeg, ValueLayout.JAVA_BYTE, 0, data.length);
        structSeg.set(ValueLayout.ADDRESS, STR_DATA_OFFSET, dataSeg);
        structSeg.set(ValueLayout.JAVA_LONG, STR_LEN_OFFSET, (long) data.length);
        return structSeg;
    }

    /**
     * Allocates an {@code aegon_str_t} slice referencing an existing {@link MemorySegment}.
     *
     * @param arena memory arena
     * @param segment memory segment
     * @return {@link MemorySegment} formatted according to {@link #STR_LAYOUT}
     */
    public static MemorySegment makeStr(Arena arena, MemorySegment segment) {
        MemorySegment structSeg = arena.allocate(STR_LAYOUT);
        if (segment == null || segment.equals(MemorySegment.NULL) || segment.byteSize() == 0) {
            structSeg.set(ValueLayout.ADDRESS, STR_DATA_OFFSET, MemorySegment.NULL);
            structSeg.set(ValueLayout.JAVA_LONG, STR_LEN_OFFSET, 0L);
            return structSeg;
        }
        structSeg.set(ValueLayout.ADDRESS, STR_DATA_OFFSET, segment);
        structSeg.set(ValueLayout.JAVA_LONG, STR_LEN_OFFSET, segment.byteSize());
        return structSeg;
    }

    /**
     * Reads a UTF-8 Java String from an {@code aegon_str_t} struct segment.
     *
     * @param strStruct {@link MemorySegment} representing {@code aegon_str_t}
     * @return decoded String, or empty string if null or zero-length
     */
    public static String readStr(MemorySegment strStruct) {
        if (strStruct == null || strStruct.equals(MemorySegment.NULL)) {
            return "";
        }
        MemorySegment dataPtr = strStruct.get(ValueLayout.ADDRESS, STR_DATA_OFFSET);
        long len = strStruct.get(ValueLayout.JAVA_LONG, STR_LEN_OFFSET);
        if (dataPtr.equals(MemorySegment.NULL) || len <= 0) {
            return "";
        }
        MemorySegment slice = dataPtr.reinterpret(len);
        byte[] bytes = new byte[(int) len];
        MemorySegment.copy(slice, ValueLayout.JAVA_BYTE, 0, bytes, 0, (int) len);
        return new String(bytes, StandardCharsets.UTF_8);
    }

    /**
     * Reads off-heap bytes from an {@code aegon_str_t} struct segment.
     *
     * @param strStruct {@link MemorySegment} representing {@code aegon_str_t}
     * @return byte array
     */
    public static byte[] readStrBytes(MemorySegment strStruct) {
        if (strStruct == null || strStruct.equals(MemorySegment.NULL)) {
            return new byte[0];
        }
        MemorySegment dataPtr = strStruct.get(ValueLayout.ADDRESS, STR_DATA_OFFSET);
        long len = strStruct.get(ValueLayout.JAVA_LONG, STR_LEN_OFFSET);
        if (dataPtr.equals(MemorySegment.NULL) || len <= 0) {
            return new byte[0];
        }
        MemorySegment slice = dataPtr.reinterpret(len);
        byte[] bytes = new byte[(int) len];
        MemorySegment.copy(slice, ValueLayout.JAVA_BYTE, 0, bytes, 0, (int) len);
        return bytes;
    }

    /**
     * Obtains a zero-copy {@link MemorySegment} slice directly referencing the off-heap buffer of an {@code aegon_str_t}.
     *
     * @param strStruct {@link MemorySegment} representing {@code aegon_str_t}
     * @return zero-copy MemorySegment or {@link MemorySegment#NULL}
     */
    public static MemorySegment readStrSegment(MemorySegment strStruct) {
        if (strStruct == null || strStruct.equals(MemorySegment.NULL)) {
            return MemorySegment.NULL;
        }
        MemorySegment dataPtr = strStruct.get(ValueLayout.ADDRESS, STR_DATA_OFFSET);
        long len = strStruct.get(ValueLayout.JAVA_LONG, STR_LEN_OFFSET);
        if (dataPtr.equals(MemorySegment.NULL) || len <= 0) {
            return MemorySegment.NULL;
        }
        return dataPtr.reinterpret(len);
    }

    /**
     * Reads a NUL-terminated C string pointer as a Java {@link String}.
     *
     * @param cstrPtr pointer to NUL-terminated C string
     * @return Java string, or {@code null} if pointer is null
     */
    public static String readCString(MemorySegment cstrPtr) {
        if (cstrPtr == null || cstrPtr.equals(MemorySegment.NULL)) {
            return null;
        }
        return cstrPtr.reinterpret(65536).getString(0, StandardCharsets.UTF_8);
    }

    /**
     * Checks an Aegon C ABI error return code, throwing {@link AegonNativeException} on non-zero return.
     *
     * @param errCode integer error code (0 == {@code AEGON_OK})
     */
    public static void checkErr(int errCode) {
        if (errCode == 0) return;
        String errName = "UNKNOWN";
        try {
            MemorySegment strPtr = (MemorySegment) CoreFfi.MH_aegon_err_str.invokeExact(errCode);
            errName = readCString(strPtr);
        } catch (Throwable ignored) {}

        String lastErr = "";
        try {
            MemorySegment lastErrPtr = (MemorySegment) CoreFfi.MH_aegon_last_error.invokeExact();
            lastErr = readCString(lastErrPtr);
        } catch (Throwable ignored) {}

        String msg = "Aegon error " + errCode + " (" + errName + ")";
        if (lastErr != null && !lastErr.isBlank()) {
            msg += ": " + lastErr;
        }
        throw new AegonNativeException(msg, errCode);
    }

    /**
     * Runtime exception thrown when an Aegon native C ABI call fails.
     */
    public static final class AegonNativeException extends RuntimeException {
        private final int errorCode;

        public AegonNativeException(String message, int errorCode) {
            super(message);
            this.errorCode = errorCode;
        }

        public int getErrorCode() {
            return errorCode;
        }
    }
}

