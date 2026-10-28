package com.teamzemo.aegon.json;

import java.lang.foreign.MemorySegment;

/**
 * Service Provider Interface (SPI) for JSON serialization and deserialization in Aegon Java.
 *
 * <p>The default implementation is backed by Fastjson2 for SIMD-accelerated high performance,
 * but applications can register a custom implementation via {@link Json#setBinder(JsonBinder)}.
 */
public interface JsonBinder {

    /**
     * Serializes any Java object or record into a JSON string.
     *
     * @param value Java object, record, list, or map to serialize
     * @return JSON string
     */
    String toJson(Object value);

    /**
     * Serializes any Java object or record directly into UTF-8 JSON bytes.
     *
     * @param value Java object, record, list, or map to serialize
     * @return UTF-8 encoded byte array of JSON
     */
    byte[] toJsonBytes(Object value);

    /**
     * Deserializes a JSON string into an instance of {@code targetType}.
     *
     * @param json JSON string
     * @param targetType target Java class or record
     * @param <T> deserialized object type
     * @return deserialized instance
     */
    <T> T fromJson(String json, Class<T> targetType);

    /**
     * Deserializes raw UTF-8 JSON bytes into an instance of {@code targetType}.
     *
     * @param bytes UTF-8 JSON bytes
     * @param targetType target Java class or record
     * @param <T> deserialized object type
     * @return deserialized instance
     */
    <T> T fromJson(byte[] bytes, Class<T> targetType);

    /**
     * Deserializes an off-heap {@link MemorySegment} containing UTF-8 JSON bytes into an instance of {@code targetType}.
     *
     * @param segment off-heap memory slice containing UTF-8 JSON
     * @param targetType target Java class or record
     * @param <T> deserialized object type
     * @return deserialized instance
     */
    default <T> T fromJson(MemorySegment segment, Class<T> targetType) {
        if (segment == null || segment.byteSize() == 0) {
            return null;
        }
        byte[] bytes = new byte[(int) segment.byteSize()];
        MemorySegment.copy(segment, java.lang.foreign.ValueLayout.JAVA_BYTE, 0, bytes, 0, bytes.length);
        return fromJson(bytes, targetType);
    }
}

