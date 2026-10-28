package com.teamzemo.aegon;

import com.teamzemo.aegon.internal.ffi.ServerFfi;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

/**
 * Server tuning and configuration options matching Aegon C++ {@code ServerConfig}.
 */
public final class ServerConfig {

    private final EngineType engine;
    private final int ringEntries;
    private final int bufferPoolEntries;
    private final int bufferSize;
    private final long maxUriLength;
    private final long maxHeadersSize;
    private final long maxHeaderCount;
    private final long maxBodySize;
    private final boolean tcpNoDelay;
    private final boolean tcpKeepAlive;
    private final boolean enableHttp3;

    private ServerConfig(Builder b) {
        this.engine = b.engine;
        this.ringEntries = b.ringEntries;
        this.bufferPoolEntries = b.bufferPoolEntries;
        this.bufferSize = b.bufferSize;
        this.maxUriLength = b.maxUriLength;
        this.maxHeadersSize = b.maxHeadersSize;
        this.maxHeaderCount = b.maxHeaderCount;
        this.maxBodySize = b.maxBodySize;
        this.tcpNoDelay = b.tcpNoDelay;
        this.tcpKeepAlive = b.tcpKeepAlive;
        this.enableHttp3 = b.enableHttp3;
    }

    public static Builder builder() {
        return new Builder();
    }

    public static ServerConfig defaults() {
        return builder().build();
    }

    public EngineType engine() { return engine; }
    public int ringEntries() { return ringEntries; }
    public int bufferPoolEntries() { return bufferPoolEntries; }
    public int bufferSize() { return bufferSize; }
    public long maxUriLength() { return maxUriLength; }
    public long maxHeadersSize() { return maxHeadersSize; }
    public long maxHeaderCount() { return maxHeaderCount; }
    public long maxBodySize() { return maxBodySize; }
    public boolean tcpNoDelay() { return tcpNoDelay; }
    public boolean tcpKeepAlive() { return tcpKeepAlive; }
    public boolean enableHttp3() { return enableHttp3; }

    /**
     * Allocates and populates an {@code aegon_server_config_t} native segment.
     *
     * @param arena memory arena
     * @return populated native memory segment
     */
    public MemorySegment toNative(Arena arena) {
        // Allocate generous size (e.g. 256 bytes) covering aegon_server_config_t
        MemorySegment cfg = arena.allocate(256);
        try {
            ServerFfi.MH_aegon_server_config_init.invokeExact(cfg);

            // Override engine
            cfg.set(ValueLayout.JAVA_INT, 8, engine.nativeCode());
            cfg.set(ValueLayout.JAVA_INT, 12, ringEntries);
            cfg.set(ValueLayout.JAVA_SHORT, 16, (short) bufferPoolEntries);
            cfg.set(ValueLayout.JAVA_SHORT, 18, (short) bufferSize);

            // Protocol limits (offsets with alignment: 24, 32, 40, 48)
            cfg.set(ValueLayout.JAVA_LONG, 24, maxUriLength);
            cfg.set(ValueLayout.JAVA_LONG, 32, maxHeadersSize);
            cfg.set(ValueLayout.JAVA_LONG, 40, maxHeaderCount);
            cfg.set(ValueLayout.JAVA_LONG, 48, maxBodySize);

            cfg.set(ValueLayout.JAVA_BOOLEAN, 56, tcpNoDelay);
            cfg.set(ValueLayout.JAVA_BOOLEAN, 57, tcpKeepAlive);
            return cfg;
        } catch (Throwable t) {
            throw new RuntimeException("Failed to construct native server config: " + t.getMessage(), t);
        }
    }

    public static final class Builder {
        private EngineType engine = EngineType.AUTO;
        private int ringEntries = 4096;
        private int bufferPoolEntries = 8192;
        private int bufferSize = 4096;
        private long maxUriLength = 8192;
        private long maxHeadersSize = 65536;
        private long maxHeaderCount = 100;
        private long maxBodySize = 16 * 1024 * 1024;
        private boolean tcpNoDelay = true;
        private boolean tcpKeepAlive = true;
        private boolean enableHttp3 = true;

        public Builder engine(EngineType engine) {
            this.engine = engine;
            return this;
        }

        public Builder ringEntries(int ringEntries) {
            this.ringEntries = ringEntries;
            return this;
        }

        public Builder bufferPoolEntries(int bufferPoolEntries) {
            this.bufferPoolEntries = bufferPoolEntries;
            return this;
        }

        public Builder bufferSize(int bufferSize) {
            this.bufferSize = bufferSize;
            return this;
        }

        public Builder maxUriLength(long maxUriLength) {
            this.maxUriLength = maxUriLength;
            return this;
        }

        public Builder maxHeadersSize(long maxHeadersSize) {
            this.maxHeadersSize = maxHeadersSize;
            return this;
        }

        public Builder maxHeaderCount(long maxHeaderCount) {
            this.maxHeaderCount = maxHeaderCount;
            return this;
        }

        public Builder maxBodySize(long maxBodySize) {
            this.maxBodySize = maxBodySize;
            return this;
        }

        public Builder tcpNoDelay(boolean tcpNoDelay) {
            this.tcpNoDelay = tcpNoDelay;
            return this;
        }

        public Builder tcpKeepAlive(boolean tcpKeepAlive) {
            this.tcpKeepAlive = tcpKeepAlive;
            return this;
        }

        public Builder enableHttp3(boolean enableHttp3) {
            this.enableHttp3 = enableHttp3;
            return this;
        }

        public ServerConfig build() {
            return new ServerConfig(this);
        }
    }
}

