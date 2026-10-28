package com.teamzemo.aegon.json;

import com.alibaba.fastjson2.JSON;

/**
 * Ultra-fast JSON binder implementation powered by Alibaba Fastjson2.
 *
 * <p>Leverages SIMD vectorization and zero-allocation parsing for record and POJO mapping.
 */
public final class FastJsonBinder implements JsonBinder {

    public static final FastJsonBinder INSTANCE = new FastJsonBinder();

    public FastJsonBinder() {}

    @Override
    public String toJson(Object value) {
        if (value == null) {
            return "null";
        }
        return JSON.toJSONString(value);
    }

    @Override
    public byte[] toJsonBytes(Object value) {
        if (value == null) {
            return new byte[]{'n', 'u', 'l', 'l'};
        }
        return JSON.toJSONBytes(value);
    }

    @Override
    public <T> T fromJson(String json, Class<T> targetType) {
        if (json == null || json.isBlank()) {
            return null;
        }
        return JSON.parseObject(json, targetType);
    }

    @Override
    public <T> T fromJson(byte[] bytes, Class<T> targetType) {
        if (bytes == null || bytes.length == 0) {
            return null;
        }
        return JSON.parseObject(bytes, targetType);
    }
}

