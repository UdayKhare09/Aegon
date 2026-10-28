package com.teamzemo.aegon.multipart;

import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;

/**
 * Uploaded file part from a multipart/form-data request.
 */
public final class MultipartFile {

    private final String name;
    private final String filename;
    private final String contentType;
    private final byte[] data;
    private final MemorySegment dataSegment;

    public MultipartFile(String name, String filename, String contentType, byte[] data, MemorySegment dataSegment) {
        this.name = name != null ? name : "";
        this.filename = filename != null ? filename : "";
        this.contentType = contentType != null ? contentType : "application/octet-stream";
        this.data = data != null ? data : new byte[0];
        this.dataSegment = dataSegment != null ? dataSegment : MemorySegment.NULL;
    }

    /**
     * Form field name of this file part.
     *
     * @return field name
     */
    public String name() {
        return name;
    }

    /**
     * Original client filename (e.g. "photo.jpg").
     *
     * @return filename
     */
    public String filename() {
        return filename;
    }

    /**
     * MIME Content-Type of the file part.
     *
     * @return content type
     */
    public String contentType() {
        return contentType;
    }

    /**
     * Raw payload bytes of the file.
     *
     * @return byte array
     */
    public byte[] data() {
        return data;
    }

    /**
     * Direct zero-copy off-heap memory segment of the file payload.
     *
     * @return memory segment
     */
    public MemorySegment dataSegment() {
        return dataSegment;
    }

    /**
     * Decodes the file payload as a UTF-8 text string.
     *
     * @return text string
     */
    public String asText() {
        return new String(data, StandardCharsets.UTF_8);
    }
}

