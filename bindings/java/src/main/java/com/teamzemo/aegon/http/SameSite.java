package com.teamzemo.aegon.http;

/**
 * Cookie SameSite policy mirroring Aegon C++ {@code aegon::http::SameSite}.
 */
public enum SameSite {
    DEFAULT(0),
    LAX(1),
    STRICT(2),
    NONE(3);

    private final int nativeCode;

    SameSite(int nativeCode) {
        this.nativeCode = nativeCode;
    }

    /**
     * Returns the C ABI integer code for this SameSite setting.
     *
     * @return integer enum code
     */
    public int nativeCode() {
        return nativeCode;
    }
}

