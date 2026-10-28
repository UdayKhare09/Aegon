package com.teamzemo.aegon.middleware;

/**
 * Static file serving options matching Aegon C++ {@code StaticFilesOptions}.
 */
public final class StaticFilesConfig {

    private final boolean precompressed;
    private final boolean cacheInMemory;
    private final String indexFile;
    private final boolean spa;
    private final String fallbackFile;
    private final boolean etag;
    private final boolean lastModified;

    private StaticFilesConfig(Builder b) {
        this.precompressed = b.precompressed;
        this.cacheInMemory = b.cacheInMemory;
        this.indexFile = b.indexFile != null ? b.indexFile : "index.html";
        this.spa = b.spa;
        this.fallbackFile = b.fallbackFile != null ? b.fallbackFile : "index.html";
        this.etag = b.etag;
        this.lastModified = b.lastModified;
    }

    public static Builder builder() {
        return new Builder();
    }

    public static StaticFilesConfig defaults() {
        return builder().build();
    }

    public boolean precompressed() { return precompressed; }
    public boolean cacheInMemory() { return cacheInMemory; }
    public String indexFile() { return indexFile; }
    public boolean spa() { return spa; }
    public String fallbackFile() { return fallbackFile; }
    public boolean etag() { return etag; }
    public boolean lastModified() { return lastModified; }

    public static final class Builder {
        private boolean precompressed = true;
        private boolean cacheInMemory = true;
        private String indexFile = "index.html";
        private boolean spa = false;
        private String fallbackFile = "index.html";
        private boolean etag = true;
        private boolean lastModified = true;

        public Builder precompressed(boolean enable) {
            this.precompressed = enable;
            return this;
        }

        public Builder cacheInMemory(boolean enable) {
            this.cacheInMemory = enable;
            return this;
        }

        public Builder indexFile(String indexFile) {
            this.indexFile = indexFile;
            return this;
        }

        public Builder spa(boolean enable) {
            this.spa = enable;
            return this;
        }

        public Builder fallbackFile(String fallbackFile) {
            this.fallbackFile = fallbackFile;
            return this;
        }

        public Builder etag(boolean enable) {
            this.etag = enable;
            return this;
        }

        public Builder lastModified(boolean enable) {
            this.lastModified = enable;
            return this;
        }

        public StaticFilesConfig build() {
            return new StaticFilesConfig(this);
        }
    }
}

