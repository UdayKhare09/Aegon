package com.teamzemo.aegon.http;

import java.util.Objects;

/**
 * Configuration options for outgoing HTTP cookies (RFC 6265bis).
 */
public final class CookieOptions {

    private final String name;
    private final String value;
    private final String path;
    private final String domain;
    private final long maxAgeSeconds;
    private final SameSite sameSite;
    private final boolean httpOnly;
    private final boolean secure;
    private final boolean partitioned;

    private CookieOptions(Builder b) {
        this.name = Objects.requireNonNull(b.name, "Cookie name cannot be null");
        this.value = b.value != null ? b.value : "";
        this.path = b.path != null ? b.path : "/";
        this.domain = b.domain != null ? b.domain : "";
        this.maxAgeSeconds = b.maxAgeSeconds;
        this.sameSite = b.sameSite != null ? b.sameSite : SameSite.DEFAULT;
        this.httpOnly = b.httpOnly;
        this.secure = b.secure;
        this.partitioned = b.partitioned;
    }

    /**
     * Creates a new cookie builder with the given name and value.
     *
     * @param name cookie name
     * @param value cookie value
     * @return new {@link Builder}
     */
    public static Builder builder(String name, String value) {
        return new Builder(name, value);
    }

    /**
     * Creates simple cookie options with default path "/" and no expiration.
     *
     * @param name cookie name
     * @param value cookie value
     * @return {@link CookieOptions}
     */
    public static CookieOptions of(String name, String value) {
        return builder(name, value).build();
    }

    public String name() { return name; }
    public String value() { return value; }
    public String path() { return path; }
    public String domain() { return domain; }
    public long maxAgeSeconds() { return maxAgeSeconds; }
    public SameSite sameSite() { return sameSite; }
    public boolean httpOnly() { return httpOnly; }
    public boolean secure() { return secure; }
    public boolean partitioned() { return partitioned; }

    /**
     * Fluent builder for {@link CookieOptions}.
     */
    public static final class Builder {
        private final String name;
        private String value;
        private String path = "/";
        private String domain = "";
        private long maxAgeSeconds = -1;
        private SameSite sameSite = SameSite.DEFAULT;
        private boolean httpOnly = true;
        private boolean secure = false;
        private boolean partitioned = false;

        public Builder(String name, String value) {
            this.name = name;
            this.value = value;
        }

        public Builder value(String value) {
            this.value = value;
            return this;
        }

        public Builder path(String path) {
            this.path = path;
            return this;
        }

        public Builder domain(String domain) {
            this.domain = domain;
            return this;
        }

        public Builder maxAgeSeconds(long seconds) {
            this.maxAgeSeconds = seconds;
            return this;
        }

        public Builder sameSite(SameSite sameSite) {
            this.sameSite = sameSite;
            return this;
        }

        public Builder httpOnly(boolean httpOnly) {
            this.httpOnly = httpOnly;
            return this;
        }

        public Builder secure(boolean secure) {
            this.secure = secure;
            return this;
        }

        public Builder partitioned(boolean partitioned) {
            this.partitioned = partitioned;
            return this;
        }

        public CookieOptions build() {
            return new CookieOptions(this);
        }
    }
}

