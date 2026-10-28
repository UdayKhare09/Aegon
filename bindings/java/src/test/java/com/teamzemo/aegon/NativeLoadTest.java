package com.teamzemo.aegon;

import com.teamzemo.aegon.internal.ffi.CoreFfi;
import com.teamzemo.aegon.internal.ffi.FfiSupport;
import org.junit.jupiter.api.Test;

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
}
