package com.teamzemo.aegon.ws;

/**
 * RFC 6455 WebSocket frame opcodes.
 */
public enum WsOpcode {
    CONTINUATION(0x0),
    TEXT(0x1),
    BINARY(0x2),
    CLOSE(0x8),
    PING(0x9),
    PONG(0xA);

    private final int nativeCode;

    WsOpcode(int nativeCode) {
        this.nativeCode = nativeCode;
    }

    public int nativeCode() {
        return nativeCode;
    }

    public static WsOpcode fromNativeCode(int code) {
        for (WsOpcode op : values()) {
            if (op.nativeCode == code) return op;
        }
        return BINARY;
    }
}

