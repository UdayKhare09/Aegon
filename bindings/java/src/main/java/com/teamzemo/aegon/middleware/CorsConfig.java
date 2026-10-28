package com.teamzemo.aegon.middleware;

import java.util.*;

/**
 * Cross-Origin Resource Sharing (CORS) middleware configuration matching Aegon C++ {@code CorsConfig}.
 */
public final class CorsConfig {

    private final List<String> allowedOrigins;
    private final List<String> originPatterns;
    private final int allowedMethodsMask;
    private final List<String> allowedHeaders;
    private final List<String> exposeHeaders;
    private final boolean allowCredentials;
    private final boolean allowPrivateNetwork;
    private final int maxAgeSeconds;
    private final boolean varyHeader;

    private CorsConfig(Builder b) {
        this.allowedOrigins = List.copyOf(b.allowedOrigins);
        this.originPatterns = List.copyOf(b.originPatterns);
        this.allowedMethodsMask = b.allowedMethodsMask;
        this.allowedHeaders = List.copyOf(b.allowedHeaders);
        this.exposeHeaders = List.copyOf(b.exposeHeaders);
        this.allowCredentials = b.allowCredentials;
        this.allowPrivateNetwork = b.allowPrivateNetwork;
        this.maxAgeSeconds = b.maxAgeSeconds;
        this.varyHeader = b.varyHeader;
    }

    /**
     * Creates a default permissive CORS configuration.
     *
     * @return default {@link CorsConfig}
     */
    public static CorsConfig permissive() {
        return builder().allowedOrigins("*").allowedHeaders("*").build();
    }

    public static Builder builder() {
        return new Builder();
    }

    public List<String> allowedOrigins() { return allowedOrigins; }
    public List<String> originPatterns() { return originPatterns; }
    public int allowedMethodsMask() { return allowedMethodsMask; }
    public List<String> allowedHeaders() { return allowedHeaders; }
    public List<String> exposeHeaders() { return exposeHeaders; }
    public boolean allowCredentials() { return allowCredentials; }
    public boolean allowPrivateNetwork() { return allowPrivateNetwork; }
    public int maxAgeSeconds() { return maxAgeSeconds; }
    public boolean varyHeader() { return varyHeader; }

    public static final class Builder {
        private final List<String> allowedOrigins = new ArrayList<>();
        private final List<String> originPatterns = new ArrayList<>();
        private int allowedMethodsMask = 0x1FF; // ALL
        private final List<String> allowedHeaders = new ArrayList<>();
        private final List<String> exposeHeaders = new ArrayList<>();
        private boolean allowCredentials = false;
        private boolean allowPrivateNetwork = false;
        private int maxAgeSeconds = 86400;
        private boolean varyHeader = true;

        public Builder allowedOrigins(String... origins) {
            this.allowedOrigins.addAll(Arrays.asList(origins));
            return this;
        }

        public Builder allowAllOrigins(boolean allow) {
            if (allow) {
                this.allowedOrigins.clear();
                this.allowedOrigins.add("*");
            }
            return this;
        }

        public Builder allowMethods(String... methods) {
            int mask = 0;
            for (String m : methods) {
                if ("GET".equalsIgnoreCase(m)) mask |= 0x01;
                else if ("POST".equalsIgnoreCase(m)) mask |= 0x02;
                else if ("PUT".equalsIgnoreCase(m)) mask |= 0x04;
                else if ("DELETE".equalsIgnoreCase(m)) mask |= 0x08;
                else if ("PATCH".equalsIgnoreCase(m)) mask |= 0x10;
                else if ("HEAD".equalsIgnoreCase(m)) mask |= 0x20;
                else if ("OPTIONS".equalsIgnoreCase(m)) mask |= 0x40;
            }
            this.allowedMethodsMask = mask != 0 ? mask : 0x1FF;
            return this;
        }

        public Builder originPatterns(String... patterns) {
            this.originPatterns.addAll(Arrays.asList(patterns));
            return this;
        }

        public Builder allowedHeaders(String... headers) {
            this.allowedHeaders.addAll(Arrays.asList(headers));
            return this;
        }

        public Builder allowHeaders(String... headers) {
            return allowedHeaders(headers);
        }

        public Builder exposeHeaders(String... headers) {
            this.exposeHeaders.addAll(Arrays.asList(headers));
            return this;
        }

        public Builder allowCredentials(boolean allow) {
            this.allowCredentials = allow;
            return this;
        }

        public Builder allowPrivateNetwork(boolean allow) {
            this.allowPrivateNetwork = allow;
            return this;
        }

        public Builder maxAgeSeconds(int seconds) {
            this.maxAgeSeconds = seconds;
            return this;
        }

        public Builder varyHeader(boolean vary) {
            this.varyHeader = vary;
            return this;
        }

        public CorsConfig build() {
            return new CorsConfig(this);
        }
    }
}
