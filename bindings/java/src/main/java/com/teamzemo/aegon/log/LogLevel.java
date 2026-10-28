package com.teamzemo.aegon.log;

/**
 * Logging severity levels matching Aegon C++ {@code aegon::log::Level} and {@code aegon_log_level_t}.
 */
public enum LogLevel {
    TRACE(0),
    DEBUG(1),
    INFO(2),
    WARN(3),
    ERROR(4),
    FATAL(5),
    OFF(6);

    private final int code;

    LogLevel(int code) {
        this.code = code;
    }

    public int code() {
        return code;
    }

    public static LogLevel fromCode(int code) {
        for (LogLevel l : values()) {
            if (l.code == code) return l;
        }
        return INFO;
    }
}
