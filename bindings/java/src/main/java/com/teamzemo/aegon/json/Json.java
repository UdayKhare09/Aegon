package com.teamzemo.aegon.json;

import java.lang.foreign.MemorySegment;
import java.util.Objects;

/**
 * Global accessor and configuration point for JSON operations in Aegon Java.
 */
public final class Json {

    private static volatile JsonBinder activeBinder = FastJsonBinder.INSTANCE;

    private Json() {}

    /**
     * Replaces the active JSON binder implementation.
     *
     * @param binder non-null {@link JsonBinder} implementation
     */
    public static void setBinder(JsonBinder binder) {
        activeBinder = Objects.requireNonNull(binder, "binder cannot be null");
    }

    /**
     * Returns the currently active JSON binder.
     *
     * @return current {@link JsonBinder}
     */
    public static JsonBinder getBinder() {
        return activeBinder;
    }

    /**
     * Serializes any object to JSON string using the active binder.
     *
     * @param obj object to serialize
     * @return JSON string
     */
    public static String encode(Object obj) {
        return activeBinder.toJson(obj);
    }

    /**
     * Serializes any object directly to UTF-8 JSON bytes using the active binder.
     *
     * @param obj object to serialize
     * @return UTF-8 JSON bytes
     */
    public static byte[] encodeBytes(Object obj) {
        return activeBinder.toJsonBytes(obj);
    }

    /**
     * Deserializes a JSON string into a target type using the active binder.
     *
     * @param json JSON string
     * @param targetType target class or record
     * @param <T> type
     * @return deserialized instance
     */
    public static <T> T decode(String json, Class<T> targetType) {
        return activeBinder.fromJson(json, targetType);
    }

    /**
     * Deserializes UTF-8 JSON bytes into a target type using the active binder.
     *
     * @param bytes JSON bytes
     * @param targetType target class or record
     * @param <T> type
     * @return deserialized instance
     */
    public static <T> T decode(byte[] bytes, Class<T> targetType) {
        return activeBinder.fromJson(bytes, targetType);
    }

    /**
     * Deserializes an off-heap memory segment containing UTF-8 JSON bytes into a target type.
     *
     * @param segment memory slice
     * @param targetType target class or record
     * @param <T> type
     * @return deserialized instance
     */
    public static <T> T decode(MemorySegment segment, Class<T> targetType) {
        return activeBinder.fromJson(segment, targetType);
    }
}

