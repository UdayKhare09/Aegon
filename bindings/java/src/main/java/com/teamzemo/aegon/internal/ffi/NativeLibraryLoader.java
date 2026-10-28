package com.teamzemo.aegon.internal.ffi;

import java.io.File;
import java.io.InputStream;
import java.lang.foreign.Arena;
import java.lang.foreign.SymbolLookup;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;

/**
 * Utility responsible for discovering, extracting, and loading the native Aegon C library ({@code libaegon_c.so} / {@code aegon_c.dll}).
 *
 * <p>Lookup strategy:
 * <ol>
 *   <li>System property {@code aegon.library.path}</li>
 *   <li>Workspace build directory (e.g. {@code build/libaegon_c.so} or {@code ../../build/libaegon_c.so})</li>
 *   <li>Standard {@code java.library.path} and OS system paths (via {@link System#loadLibrary(String)})</li>
 *   <li>Bundled native library inside the JAR classpath under {@code /native/<os>-<arch>/}</li>
 * </ol>
 */
public final class NativeLibraryLoader {

    private static final String LIB_BASE_NAME = "aegon_c";
    private static volatile SymbolLookup SYMBOL_LOOKUP;

    private NativeLibraryLoader() {}

    /**
     * Resolves and returns the {@link SymbolLookup} instance for the linked Aegon C library.
     *
     * @return active {@link SymbolLookup} for Aegon C ABI symbols
     * @throws UnsatisfiedLinkError if the library could not be located or loaded
     */
    public static SymbolLookup lookup() {
        if (SYMBOL_LOOKUP == null) {
            synchronized (NativeLibraryLoader.class) {
                if (SYMBOL_LOOKUP == null) {
                    SYMBOL_LOOKUP = loadLibrary();
                }
            }
        }
        return SYMBOL_LOOKUP;
    }

    private static SymbolLookup loadLibrary() {
        // 1. Check explicit system property
        String explicitPath = System.getProperty("aegon.library.path");
        if (explicitPath != null && !explicitPath.isBlank()) {
            File file = new File(explicitPath);
            if (file.exists() && file.isFile()) {
                System.load(file.getAbsolutePath());
                return SymbolLookup.libraryLookup(Path.of(file.getAbsolutePath()), Arena.global());
            }
        }

        // 2. Search local build / development paths
        String os = System.getProperty("os.name", "").toLowerCase();
        String libFileName = os.contains("win") ? "aegon_c.dll"
                : (os.contains("mac") || os.contains("darwin") ? "libaegon_c.dylib" : "libaegon_c.so");

        List<String> probePaths = List.of(
                libFileName,
                "build/" + libFileName,
                "../build/" + libFileName,
                "../../build/" + libFileName,
                "../../../build/" + libFileName,
                "/usr/local/lib/" + libFileName,
                "/usr/lib/" + libFileName
        );

        for (String probe : probePaths) {
            File f = new File(probe);
            if (f.exists() && f.isFile()) {
                try {
                    String abs = f.getAbsolutePath();
                    System.load(abs);
                    return SymbolLookup.libraryLookup(Path.of(abs), Arena.global());
                } catch (Throwable ignored) {
                    // try next
                }
            }
        }

        // 3. Try standard system library loading
        try {
            System.loadLibrary(LIB_BASE_NAME);
            return SymbolLookup.loaderLookup();
        } catch (Throwable ignored) {
            // fall through to jar extraction
        }

        // 4. Try extracting from classpath resources
        String arch = System.getProperty("os.arch", "").toLowerCase();
        String normalizedOs = os.contains("win") ? "windows" : (os.contains("mac") ? "macos" : "linux");
        String normalizedArch = (arch.contains("x86_64") || arch.contains("amd64")) ? "x86_64" : (arch.contains("aarch64") || arch.contains("arm64") ? "aarch64" : arch);
        String resourcePath = "/native/" + normalizedOs + "-" + normalizedArch + "/" + libFileName;

        try (InputStream in = NativeLibraryLoader.class.getResourceAsStream(resourcePath)) {
            if (in != null) {
                Path tempFile = Files.createTempFile("aegon_c_", "_" + libFileName);
                tempFile.toFile().deleteOnExit();
                Files.copy(in, tempFile, StandardCopyOption.REPLACE_EXISTING);
                System.load(tempFile.toAbsolutePath().toString());
                return SymbolLookup.libraryLookup(tempFile, Arena.global());
            }
        } catch (Throwable ignored) {}

        throw new UnsatisfiedLinkError(
                "Failed to locate and load native Aegon C library (" + libFileName + "). " +
                "Please ensure libaegon_c.so / aegon_c.dll is in java.library.path or specify -Daegon.library.path=/path/to/" + libFileName
        );
    }
}

