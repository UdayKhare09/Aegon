package com.teamzemo.aegon.middleware;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * HTTP access logging middleware configuration.
 */
public final class AccessLogConfig {

    private final AccessLogFormat format;
    private final boolean logQuery;
    private final boolean logClientIp;
    private final List<String> skipPaths;
    private final String correlationHeader;

    private AccessLogConfig(Builder b) {
        this.format = b.format;
        this.logQuery = b.logQuery;
        this.logClientIp = b.logClientIp;
        this.skipPaths = List.copyOf(b.skipPaths);
        this.correlationHeader = b.correlationHeader;
    }

    public static Builder builder() {
        return new Builder();
    }

    public static AccessLogConfig defaults() {
        return builder().build();
    }

    public AccessLogFormat format() { return format; }
    public boolean logQuery() { return logQuery; }
    public boolean logClientIp() { return logClientIp; }
    public List<String> skipPaths() { return skipPaths; }
    public String correlationHeader() { return correlationHeader; }

    public static final class Builder {
        private AccessLogFormat format = AccessLogFormat.COMBINED;
        private boolean logQuery = true;
        private boolean logClientIp = true;
        private final List<String> skipPaths = new ArrayList<>();
        private String correlationHeader = "x-request-id";

        public Builder format(AccessLogFormat format) {
            this.format = format;
            return this;
        }

        public Builder logQuery(boolean enable) {
            this.logQuery = enable;
            return this;
        }

        public Builder logClientIp(boolean enable) {
            this.logClientIp = enable;
            return this;
        }

        public Builder skipPaths(String... paths) {
            this.skipPaths.addAll(Arrays.asList(paths));
            return this;
        }

        public Builder correlationHeader(String header) {
            this.correlationHeader = header;
            return this;
        }

        public AccessLogConfig build() {
            return new AccessLogConfig(this);
        }
    }
}

