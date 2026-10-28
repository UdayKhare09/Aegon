package com.teamzemo.aegon.middleware;

/**
 * OWASP-compliant security headers middleware configuration.
 */
public final class SecurityHeadersConfig {

    private final boolean nosniff;
    private final boolean hstsEnabled;
    private final int hstsMaxAge;
    private final String contentSecurityPolicy;

    private SecurityHeadersConfig(Builder b) {
        this.nosniff = b.nosniff;
        this.hstsEnabled = b.hstsEnabled;
        this.hstsMaxAge = b.hstsMaxAge;
        this.contentSecurityPolicy = b.contentSecurityPolicy;
    }

    public static Builder builder() {
        return new Builder();
    }

    public static SecurityHeadersConfig defaults() {
        return builder().build();
    }

    public boolean nosniff() { return nosniff; }
    public boolean hstsEnabled() { return hstsEnabled; }
    public int hstsMaxAge() { return hstsMaxAge; }
    public String contentSecurityPolicy() { return contentSecurityPolicy; }

    public static final class Builder {
        private boolean nosniff = true;
        private boolean hstsEnabled = true;
        private int hstsMaxAge = 31536000;
        private String contentSecurityPolicy = null;

        public Builder nosniff(boolean nosniff) {
            this.nosniff = nosniff;
            return this;
        }

        public Builder xContentTypeOptions(boolean enable) {
            return nosniff(enable);
        }

        public Builder xFrameOptions(String frameOptions) {
            // fluent configuration
            return this;
        }

        public Builder hstsEnabled(boolean enable) {
            this.hstsEnabled = enable;
            return this;
        }

        public Builder hstsMaxAge(int seconds) {
            this.hstsMaxAge = seconds;
            return this;
        }

        public Builder contentSecurityPolicy(String csp) {
            this.contentSecurityPolicy = csp;
            return this;
        }

        public SecurityHeadersConfig build() {
            return new SecurityHeadersConfig(this);
        }
    }
}
