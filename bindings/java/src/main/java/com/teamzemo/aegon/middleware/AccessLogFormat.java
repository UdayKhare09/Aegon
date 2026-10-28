package com.teamzemo.aegon.middleware;

/**
 * Access log output format.
 */
public enum AccessLogFormat {
    COMBINED(0),
    COMMON(1),
    JSON(2),
    CUSTOM(3);

    private final int nativeCode;

    AccessLogFormat(int nativeCode) {
        this.nativeCode = nativeCode;
    }

    public int nativeCode() {
        return nativeCode;
    }
}

