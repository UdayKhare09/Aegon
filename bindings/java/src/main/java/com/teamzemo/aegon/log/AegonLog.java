package com.teamzemo.aegon.log;

import com.teamzemo.aegon.internal.ffi.FfiSupport;
import com.teamzemo.aegon.internal.ffi.LogFfi;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

/**
 * Access to Aegon's native SIMD-accelerated logging subsystem matching {@code aegon::log}.
 */
public final class AegonLog {

    private AegonLog() {}

    /**
     * Initializes the global Aegon native logger.
     *
     * @param level default log level
     * @param asyncLog whether to use a background flush thread
     */
    public static void init(LogLevel level, boolean asyncLog) {
        try {
            int err = (int) LogFfi.MH_aegon_log_init.invokeExact(level.code(), asyncLog);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to initialize Aegon log: " + t.getMessage(), t);
        }
    }

    /**
     * Dynamically sets the runtime log severity threshold.
     *
     * @param level new log level
     */
    public static void setLevel(LogLevel level) {
        try {
            LogFfi.MH_aegon_log_set_level.invokeExact(level.code());
        } catch (Throwable t) {
            throw new RuntimeException("Failed to set log level: " + t.getMessage(), t);
        }
    }

    /**
     * Queries the current runtime log severity threshold.
     *
     * @return current {@link LogLevel}
     */
    public static LogLevel getLevel() {
        try {
            int code = (int) LogFfi.MH_aegon_log_level.invokeExact();
            return LogLevel.fromCode(code);
        } catch (Throwable t) {
            return LogLevel.INFO;
        }
    }

    /**
     * Adds an append-mode rolling file sink for log outputs.
     *
     * @param path file path on disk
     */
    public static void addFileSink(String path) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment pathSeg = arena.allocateFrom(path);
            int err = (int) LogFfi.MH_aegon_log_add_file_sink.invokeExact(pathSeg);
            FfiSupport.checkErr(err);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to add file sink: " + t.getMessage(), t);
        }
    }

    /**
     * Clears all log sinks (disabling output until another sink is added).
     */
    public static void clearSinks() {
        try {
            LogFfi.MH_aegon_log_clear_sinks.invokeExact();
        } catch (Throwable t) {
            throw new RuntimeException(t);
        }
    }

    /**
     * Emits a raw log record through Aegon's native logging pipeline.
     *
     * @param level severity level
     * @param message log message string
     */
    public static void write(LogLevel level, String message) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment strSeg = FfiSupport.makeStr(arena, message);
            LogFfi.MH_aegon_log_write.invokeExact(level.code(), strSeg);
        } catch (Throwable t) {
            throw new RuntimeException(t);
        }
    }

    public static void trace(String message) { write(LogLevel.TRACE, message); }
    public static void debug(String message) { write(LogLevel.DEBUG, message); }
    public static void info(String message)  { write(LogLevel.INFO,  message); }
    public static void warn(String message)  { write(LogLevel.WARN,  message); }
    public static void error(String message) { write(LogLevel.ERROR, message); }
    public static void fatal(String message) { write(LogLevel.FATAL, message); }

    /**
     * Flushes buffered log records.
     */
    public static void flush() {
        try {
            LogFfi.MH_aegon_log_flush.invokeExact();
        } catch (Throwable ignored) {}
    }

    /**
     * Shuts down the background logging worker thread.
     */
    public static void shutdown() {
        try {
            LogFfi.MH_aegon_log_shutdown.invokeExact();
        } catch (Throwable ignored) {}
    }
}
