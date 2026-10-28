package com.teamzemo.aegon;

import com.teamzemo.aegon.internal.ffi.CoreFfi;
import com.teamzemo.aegon.internal.ffi.FfiSupport;
import com.teamzemo.aegon.log.AegonLog;
import com.teamzemo.aegon.log.LogLevel;
import org.junit.jupiter.api.Test;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

import static org.junit.jupiter.api.Assertions.*;

public class NativeLoadTest {

    @Test
    public void testNativeAbiVersion() throws Throwable {
        int abiVersion = (int) CoreFfi.MH_aegon_abi_version.invokeExact();
        assertEquals(1, abiVersion, "ABI version must match AEGON_ABI_VERSION (1)");

        int versionHex = (int) CoreFfi.MH_aegon_version.invokeExact();
        assertTrue(versionHex > 0, "Version hex must be positive");

        MemorySegment verStrPtr = (MemorySegment) CoreFfi.MH_aegon_version_string.invokeExact();
        String verStr = FfiSupport.readCString(verStrPtr);
        assertNotNull(verStr);
        assertTrue(verStr.startsWith("0.2"), "Version string should start with 0.2, got: " + verStr);
    }

    @Test
    public void testNativeLogging() {
        AegonLog.setLevel(LogLevel.DEBUG);
        assertEquals(LogLevel.DEBUG, AegonLog.getLevel());

        AegonLog.info("Testing Aegon native logger from Java FFM");
        AegonLog.flush();

        AegonLog.setLevel(LogLevel.INFO);
        assertEquals(LogLevel.INFO, AegonLog.getLevel());
    }

    @Test
    public void testNativeStringHelpersAndErrors() throws Throwable {
        // Test aegon_str and aegon_str_dup downcalls
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment cstr = arena.allocateFrom("Hello Native String");
            MemorySegment strSeg = (MemorySegment) CoreFfi.MH_aegon_str.invokeExact((java.lang.foreign.SegmentAllocator) arena, cstr);
            String read = FfiSupport.readStr(strSeg);
            assertEquals("Hello Native String", read);

            MemorySegment dupSeg = (MemorySegment) CoreFfi.MH_aegon_str_dup.invokeExact((java.lang.foreign.SegmentAllocator) arena, strSeg);
            String dupRead = FfiSupport.readStr(dupSeg);
            assertEquals("Hello Native String", dupRead);

            // Free allocated buffer
            MemorySegment dataPtr = dupSeg.get(java.lang.foreign.ValueLayout.ADDRESS, 0);
            CoreFfi.MH_aegon_mem_free.invokeExact(dataPtr);
        }

        // Test last error clear
        CoreFfi.MH_aegon_last_error_clear.invokeExact();
    }
}
