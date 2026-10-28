package com.teamzemo.aegon.http;

/**
 * Internal registry holding application-level references.
 */
public final class AegonServerRegistry {

    private static volatile Object appData;

    private AegonServerRegistry() {}

    public static void setAppData(Object data) {
        appData = data;
    }

    public static Object getAppData() {
        return appData;
    }
}

