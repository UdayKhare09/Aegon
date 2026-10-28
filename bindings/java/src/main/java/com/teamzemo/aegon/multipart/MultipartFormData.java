package com.teamzemo.aegon.multipart;

import com.teamzemo.aegon.internal.ffi.FfiSupport;
import com.teamzemo.aegon.internal.ffi.MultipartFfi;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.*;

/**
 * Parsed multipart/form-data payload containing textual form fields and uploaded files.
 */
public final class MultipartFormData implements AutoCloseable {

    private final Map<String, String> fields = new LinkedHashMap<>();
    private final Map<String, MultipartFile> filesByName = new LinkedHashMap<>();
    private final List<MultipartFile> fileList = new ArrayList<>();
    private MemorySegment rawHandle;

    private MultipartFormData(MemorySegment mpHandle) {
        this.rawHandle = mpHandle;
        extractData(mpHandle);
    }

    /**
     * Parses a multipart/form-data request from an active {@code aegon_ctx_t*} handle.
     *
     * @param ctxPtr native context pointer
     * @return parsed {@link MultipartFormData}, or {@code null} if not a valid multipart request
     */
    public static MultipartFormData parse(MemorySegment ctxPtr) {
        if (ctxPtr == null || ctxPtr.equals(MemorySegment.NULL)) {
            return null;
        }
        try {
            MemorySegment mp = (MemorySegment) MultipartFfi.MH_aegon_multipart_parse.invokeExact(ctxPtr);
            if (mp.equals(MemorySegment.NULL)) {
                return null;
            }
            return new MultipartFormData(mp);
        } catch (Throwable t) {
            return null;
        }
    }

    private void extractData(MemorySegment mp) {
        try (Arena arena = Arena.ofConfined()) {
            // 1. Extract text fields
            long fieldCount = (long) MultipartFfi.MH_aegon_multipart_field_count.invokeExact(mp);
            MemorySegment outName = arena.allocate(FfiSupport.STR_LAYOUT);
            MemorySegment outVal = arena.allocate(FfiSupport.STR_LAYOUT);

            for (long i = 0; i < fieldCount; i++) {
                boolean ok = (boolean) MultipartFfi.MH_aegon_multipart_field_at.invokeExact(mp, i, outName, outVal);
                if (ok) {
                    String name = FfiSupport.readStr(outName);
                    String val = FfiSupport.readStr(outVal);
                    fields.put(name, val);
                }
            }

            // 2. Extract files
            long fileCount = (long) MultipartFfi.MH_aegon_multipart_file_count.invokeExact(mp);
            MemorySegment fileInfo = arena.allocate(MultipartFfi.MULTIPART_FILE_INFO_LAYOUT);

            for (long i = 0; i < fileCount; i++) {
                boolean ok = (boolean) MultipartFfi.MH_aegon_multipart_file_at.invokeExact(mp, i, fileInfo);
                if (ok) {
                    MemorySegment nameSeg = fileInfo.asSlice(0, FfiSupport.STR_LAYOUT.byteSize());
                    MemorySegment filenameSeg = fileInfo.asSlice(FfiSupport.STR_LAYOUT.byteSize(), FfiSupport.STR_LAYOUT.byteSize());
                    MemorySegment typeSeg = fileInfo.asSlice(FfiSupport.STR_LAYOUT.byteSize() * 2, FfiSupport.STR_LAYOUT.byteSize());
                    MemorySegment dataSeg = fileInfo.asSlice(FfiSupport.STR_LAYOUT.byteSize() * 3, FfiSupport.STR_LAYOUT.byteSize());

                    String name = FfiSupport.readStr(nameSeg);
                    String filename = FfiSupport.readStr(filenameSeg);
                    String contentType = FfiSupport.readStr(typeSeg);
                    byte[] data = FfiSupport.readStrBytes(dataSeg);
                    MemorySegment rawSeg = FfiSupport.readStrSegment(dataSeg);

                    MultipartFile file = new MultipartFile(name, filename, contentType, data, rawSeg);
                    filesByName.put(name, file);
                    fileList.add(file);
                }
            }
        } catch (Throwable t) {
            throw new RuntimeException("Failed to read multipart data: " + t.getMessage(), t);
        }
    }

    /**
     * Retrieves a text form field by name.
     *
     * @param name form field name
     * @return {@link Optional} containing the field string, or empty if absent
     */
    public Optional<String> field(String name) {
        return Optional.ofNullable(fields.get(name));
    }

    /**
     * Returns an unmodifiable view of all text form fields.
     *
     * @return map of field names to values
     */
    public Map<String, String> fields() {
        return Collections.unmodifiableMap(fields);
    }

    /**
     * Retrieves an uploaded file part by field name.
     *
     * @param name form field name
     * @return {@link Optional} containing the {@link MultipartFile}, or empty if absent
     */
    public Optional<MultipartFile> file(String name) {
        return Optional.ofNullable(filesByName.get(name));
    }

    /**
     * Returns all uploaded file parts.
     *
     * @return unmodifiable list of uploaded files
     */
    public List<MultipartFile> files() {
        return Collections.unmodifiableList(fileList);
    }

    @Override
    public void close() {
        if (rawHandle != null && !rawHandle.equals(MemorySegment.NULL)) {
            try {
                MultipartFfi.MH_aegon_multipart_destroy.invokeExact(rawHandle);
            } catch (Throwable ignored) {}
            rawHandle = MemorySegment.NULL;
        }
    }
}

