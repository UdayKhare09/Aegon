package com.teamzemo.aegon;

/**
 * Native I/O engine preference for the Aegon server runtime.
 */
public enum EngineType {
    /**
     * Probes and selects the highest-performance engine supported by the OS (io_uring -> epoll -> IOCP).
     */
    AUTO(0),

    /**
     * High-performance Linux io_uring asynchronous engine (Linux 5.1+).
     */
    IOURING(1),

    /**
     * Standard Linux epoll edge-triggered reactor engine.
     */
    EPOLL(2),

    /**
     * BSD / macOS kqueue (reserved).
     */
    KQUEUE(3),

    /**
     * Windows I/O Completion Ports (IOCP).
     */
    IOCP(4);

    private final int nativeCode;

    EngineType(int nativeCode) {
        this.nativeCode = nativeCode;
    }

    public int nativeCode() {
        return nativeCode;
    }
}

