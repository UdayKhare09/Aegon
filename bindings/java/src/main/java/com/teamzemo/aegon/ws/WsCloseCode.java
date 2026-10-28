package com.teamzemo.aegon.ws;

/**
 * Standard RFC 6455 WebSocket close status codes.
 */
public enum WsCloseCode {
    NORMAL(1000),
    GOING_AWAY(1001),
    PROTOCOL_ERROR(1002),
    UNSUPPORTED_DATA(1003),
    NO_STATUS(1005),
    ABNORMAL(1006),
    INVALID_PAYLOAD(1007),
    POLICY_VIOLATION(1008),
    MESSAGE_TOO_BIG(1009),
    MANDATORY_EXT(1010),
    INTERNAL_ERROR(1011);

    private final int code;

    WsCloseCode(int code) {
        this.code = code;
    }

    public int code() {
        return code;
    }
}

