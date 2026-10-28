package com.teamzemo.aegon.internal.ffi;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.StructLayout;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * Native downcalls, upcall descriptors, and struct layouts for RFC 6455 WebSockets from {@code websocket.h}.
 */
public final class WebSocketFfi {

    /**
     * Struct layout for {@code aegon_ws_callbacks_t}:
     * <pre>{@code
     * typedef struct aegon_ws_callbacks {
     *     size_t struct_size;
     *     void (*on_open)   (aegon_ws_t* ws, aegon_str_t path, void* userdata);
     *     void (*on_message)(aegon_ws_t* ws, aegon_str_t payload, aegon_ws_opcode_t op, void* userdata);
     *     void (*on_close)  (aegon_ws_t* ws, uint16_t code, aegon_str_t reason, void* userdata);
     *     void (*on_error)  (aegon_ws_t* ws, aegon_str_t what, void* userdata);
     * } aegon_ws_callbacks_t;
     * }</pre>
     */
    public static final StructLayout WS_CALLBACKS_LAYOUT = MemoryLayout.structLayout(
            ValueLayout.JAVA_LONG.withName("struct_size"),
            ValueLayout.ADDRESS.withName("on_open"),
            ValueLayout.ADDRESS.withName("on_message"),
            ValueLayout.ADDRESS.withName("on_close"),
            ValueLayout.ADDRESS.withName("on_error")
    ).withName("aegon_ws_callbacks_t");

    /* Upcall Descriptors */
    public static final FunctionDescriptor WS_ON_OPEN_DESC = FunctionDescriptor.ofVoid(
            ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT, ValueLayout.ADDRESS
    );

    public static final FunctionDescriptor WS_ON_MESSAGE_DESC = FunctionDescriptor.ofVoid(
            ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT, ValueLayout.JAVA_INT, ValueLayout.ADDRESS
    );

    public static final FunctionDescriptor WS_ON_CLOSE_DESC = FunctionDescriptor.ofVoid(
            ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT, FfiSupport.STR_LAYOUT, ValueLayout.ADDRESS
    );

    public static final FunctionDescriptor WS_ON_ERROR_DESC = FunctionDescriptor.ofVoid(
            ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT, ValueLayout.ADDRESS
    );

    /* Downcalls */
    public static final MethodHandle MH_aegon_ws_callbacks_init = FfiSupport.downcall("aegon_ws_callbacks_init",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_router_ws = FfiSupport.downcall("aegon_router_ws",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_group_ws = FfiSupport.downcall("aegon_group_ws",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_router_ws_echo = FfiSupport.downcall("aegon_router_ws_echo",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    public static final MethodHandle MH_aegon_ws_send_text = FfiSupport.downcall("aegon_ws_send_text",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_ws_send_binary = FfiSupport.downcall("aegon_ws_send_binary",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_ws_send_ping = FfiSupport.downcall("aegon_ws_send_ping",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_ws_send_pong = FfiSupport.downcall("aegon_ws_send_pong",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_ws_close = FfiSupport.downcall("aegon_ws_close",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT, FfiSupport.STR_LAYOUT));

    public static final MethodHandle MH_aegon_ws_is_open = FfiSupport.downcall("aegon_ws_is_open",
            FunctionDescriptor.of(ValueLayout.JAVA_BOOLEAN, ValueLayout.ADDRESS));

    private WebSocketFfi() {}
}

