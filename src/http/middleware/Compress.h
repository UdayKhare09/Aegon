#pragma once

#include "http/Context.h"
#include "http/Middleware.h"
#include <libdeflate.h>
#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>

namespace aegon::http::middleware {

struct CompressOptions {
    int level{1};                // 1 = fastest, 12 = maximum (default 1 for ultra-high-throughput HTTP services)
    size_t min_size{256};        // Don't compress bodies smaller than this threshold (in bytes)
    bool gzip{true};             // Enable gzip compression (RFC 1952)
    bool deflate{true};          // Enable deflate compression (RFC 1950)
    bool prefer_deflate{true};   // When both gzip and deflate are acceptable, prefer deflate
    bool vary{true};             // Automatically set/merge Vary: Accept-Encoding
};

namespace detail {

// Thread-local scratch buffer to eliminate per-request heap reallocations
inline thread_local std::string tls_compress_scratch;

struct DeflateCompressorPool {
    libdeflate_compressor* comp{nullptr};
    int current_level{-1};

    libdeflate_compressor* get(int level) {
        if (!comp || current_level != level) {
            if (comp) libdeflate_free_compressor(comp);
            comp = libdeflate_alloc_compressor(level);
            current_level = level;
        }
        return comp;
    }

    ~DeflateCompressorPool() {
        if (comp) {
            libdeflate_free_compressor(comp);
            comp = nullptr;
        }
    }
};

inline thread_local DeflateCompressorPool tls_libdeflate_pool;

struct DeflateDecompressorPool {
    libdeflate_decompressor* decomp{nullptr};

    libdeflate_decompressor* get() {
        if (!decomp) {
            decomp = libdeflate_alloc_decompressor();
        }
        return decomp;
    }

    ~DeflateDecompressorPool() {
        if (decomp) {
            libdeflate_free_decompressor(decomp);
            decomp = nullptr;
        }
    }
};

inline thread_local DeflateDecompressorPool tls_libdeflate_decomp_pool;

inline bool gzip_compress(std::string_view input, std::string& output, int level) {
    if (input.empty()) return false;
    auto* compressor = tls_libdeflate_pool.get(level);
    if (!compressor) return false;

    size_t bound = libdeflate_gzip_compress_bound(compressor, input.size());
    output.resize(bound);

    size_t actual = libdeflate_gzip_compress(compressor, input.data(), input.size(), output.data(), bound);
    if (actual == 0) return false;

    output.resize(actual);
    return true;
}

inline bool deflate_compress(std::string_view input, std::string& output, int level) {
    if (input.empty()) return false;
    auto* compressor = tls_libdeflate_pool.get(level);
    if (!compressor) return false;

    size_t bound = libdeflate_zlib_compress_bound(compressor, input.size());
    output.resize(bound);

    size_t actual = libdeflate_zlib_compress(compressor, input.data(), input.size(), output.data(), bound);
    if (actual == 0) return false;

    output.resize(actual);
    return true;
}

inline bool gzip_decompress(std::string_view input, std::string& output, size_t max_out = 16 * 1024 * 1024) {
    if (input.empty()) return false;
    auto* decompressor = tls_libdeflate_decomp_pool.get();
    if (!decompressor) return false;

    output.resize(std::max(input.size() * 4, size_t(1024)));
    size_t actual_out = 0;
    auto res = libdeflate_gzip_decompress(decompressor, input.data(), input.size(), output.data(), output.size(), &actual_out);
    if (res == LIBDEFLATE_INSUFFICIENT_SPACE) {
        output.resize(std::min(max_out, output.size() * 8));
        res = libdeflate_gzip_decompress(decompressor, input.data(), input.size(), output.data(), output.size(), &actual_out);
    }
    if (res != LIBDEFLATE_SUCCESS) return false;
    output.resize(actual_out);
    return true;
}

inline bool deflate_decompress(std::string_view input, std::string& output, size_t max_out = 16 * 1024 * 1024) {
    if (input.empty()) return false;
    auto* decompressor = tls_libdeflate_decomp_pool.get();
    if (!decompressor) return false;

    output.resize(std::max(input.size() * 4, size_t(1024)));
    size_t actual_out = 0;
    auto res = libdeflate_zlib_decompress(decompressor, input.data(), input.size(), output.data(), output.size(), &actual_out);
    if (res == LIBDEFLATE_INSUFFICIENT_SPACE) {
        output.resize(std::min(max_out, output.size() * 8));
        res = libdeflate_zlib_decompress(decompressor, input.data(), input.size(), output.data(), output.size(), &actual_out);
    }
    if (res != LIBDEFLATE_SUCCESS) return false;
    output.resize(actual_out);
    return true;
}

inline void merge_vary(Response& res) {
    auto current_vary_opt = res.headers().get("vary");
    if (!current_vary_opt) {
        res.header("Vary", "Accept-Encoding");
    } else {
        std::string_view current_vary = *current_vary_opt;
        // Case-insensitive check whether Accept-Encoding is already present
        std::string lower_vary;
        lower_vary.reserve(current_vary.size());
        for (char c : current_vary) {
            lower_vary.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (lower_vary.find("accept-encoding") == std::string::npos) {
            res.set_header_owned("Vary", std::string(current_vary) + ", Accept-Encoding");
        }
    }
}

} // namespace detail

inline bool gzip_compress(std::string_view input, std::string& output, int level = 1) {
    return detail::gzip_compress(input, output, level);
}

inline bool deflate_compress(std::string_view input, std::string& output, int level = 1) {
    return detail::deflate_compress(input, output, level);
}

inline bool gzip_decompress(std::string_view input, std::string& output, size_t max_out = 16 * 1024 * 1024) {
    return detail::gzip_decompress(input, output, max_out);
}

inline bool deflate_decompress(std::string_view input, std::string& output, size_t max_out = 16 * 1024 * 1024) {
    return detail::deflate_decompress(input, output, max_out);
}

/**
 * @brief Ultra-fast SIMD HTTP compression middleware supporting gzip and deflate.
 *
 * Utilizes SIMD-vectorized libdeflate with zero-allocation thread-local compressor pools
 * and zero thread-handoff penalties. Safely preserves and merges existing Vary headers.
 */
inline MiddlewareFn Compress(CompressOptions options = {}) {
    return [options](Context& ctx, Next next) -> core::Task<void> {
        co_await next(ctx);

        // 1. Skip if client did not send Accept-Encoding
        auto accept_encoding_opt = ctx.req().headers().get("accept-encoding");
        if (!accept_encoding_opt) {
            co_return;
        }
        std::string_view accept_encoding = *accept_encoding_opt;

        // 2. Skip if already encoded (e.g. static pre-compressed assets or manual encoder)
        if (ctx.res().headers().contains("content-encoding")) {
            co_return;
        }

        // 3. Skip if status code indicates no response body
        auto status = ctx.res().status();
        if (status == StatusCode::NoContent || status == StatusCode::NotModified) {
            co_return;
        }

        // 4. Skip if body is smaller than min_size threshold
        auto body = ctx.res().body();
        if (body.size() < options.min_size) {
            co_return;
        }

        // 5. Select compression algorithm based on client Accept-Encoding
        std::string ae_lower;
        ae_lower.reserve(accept_encoding.size());
        for (char c : accept_encoding) {
            ae_lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }

        bool has_gzip = options.gzip && (ae_lower.find("gzip") != std::string::npos || ae_lower.find("*") != std::string::npos);
        bool has_deflate = options.deflate && (ae_lower.find("deflate") != std::string::npos || ae_lower.find("*") != std::string::npos);

        bool compressed = false;
        if (options.prefer_deflate && has_deflate) {
            if (detail::deflate_compress(body, detail::tls_compress_scratch, options.level)) {
                ctx.res().body(detail::tls_compress_scratch);
                ctx.res().header("Content-Encoding", "deflate");
                compressed = true;
            }
        } else if (has_gzip) {
            if (detail::gzip_compress(body, detail::tls_compress_scratch, options.level)) {
                ctx.res().body(detail::tls_compress_scratch);
                ctx.res().header("Content-Encoding", "gzip");
                compressed = true;
            }
        } else if (has_deflate) {
            if (detail::deflate_compress(body, detail::tls_compress_scratch, options.level)) {
                ctx.res().body(detail::tls_compress_scratch);
                ctx.res().header("Content-Encoding", "deflate");
                compressed = true;
            }
        }

        // 6. Safely merge Vary header
        if (compressed && options.vary) {
            detail::merge_vary(ctx.res());
        }
    };
}

} // namespace aegon::http::middleware
