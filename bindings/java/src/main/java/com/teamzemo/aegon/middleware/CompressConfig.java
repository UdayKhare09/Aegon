package com.teamzemo.aegon.middleware;

/**
 * HTTP response compression middleware configuration (libdeflate gzip / deflate).
 */
public final class CompressConfig {

    private final int level;
    private final long minSize;
    private final boolean gzip;
    private final boolean deflate;
    private final boolean preferDeflate;
    private final boolean vary;

    private CompressConfig(Builder b) {
        this.level = b.level;
        this.minSize = b.minSize;
        this.gzip = b.gzip;
        this.deflate = b.deflate;
        this.preferDeflate = b.preferDeflate;
        this.vary = b.vary;
    }

    public static Builder builder() {
        return new Builder();
    }

    public static CompressConfig defaults() {
        return builder().build();
    }

    public int level() { return level; }
    public long minSize() { return minSize; }
    public boolean gzip() { return gzip; }
    public boolean deflate() { return deflate; }
    public boolean preferDeflate() { return preferDeflate; }
    public boolean vary() { return vary; }

    public static final class Builder {
        private int level = 1;
        private long minSize = 256;
        private boolean gzip = true;
        private boolean deflate = true;
        private boolean preferDeflate = true;
        private boolean vary = true;

        public Builder level(int level) {
            this.level = level;
            return this;
        }

        public Builder minSize(long minBytes) {
            this.minSize = minBytes;
            return this;
        }

        public Builder gzip(boolean enable) {
            this.gzip = enable;
            return this;
        }

        public Builder deflate(boolean enable) {
            this.deflate = enable;
            return this;
        }

        public Builder preferDeflate(boolean prefer) {
            this.preferDeflate = prefer;
            return this;
        }

        public Builder vary(boolean vary) {
            this.vary = vary;
            return this;
        }

        public CompressConfig build() {
            return new CompressConfig(this);
        }
    }
}

