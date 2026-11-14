#pragma once

#include "http/Context.h"
#include "http/Middleware.h"
#include <libdeflate.h>
#include <brotli/encode.h>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <string_view>
#include <algorithm>
#include <cctype>

namespace aegon::http::middleware {

struct CompressOptions {
    int level{1};                // gzip/deflate (libdeflate) level: 1 = fastest, 12 = maximum
    size_t min_size{256};        // Don't compress bodies smaller than this threshold (in bytes)
    // Brotli (RFC 7932, Content-Encoding: br) for dynamic responses. Off by default: on small
    // API bodies it spends several times the CPU of libdeflate level 1 for a modest size
    // reduction, which only pays off when the network rather than the server is the
    // bottleneck. Enable it for large or bandwidth-bound responses. Static files serve
    // precompressed .br regardless of this setting.
    bool brotli{false};
    int brotli_quality{4};       // 0..11
    int brotli_lgwin{18};        // log2 of the sliding window, 10..24
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
    size_t actual = 0;
    output.resize_and_overwrite(bound, [&](char* p, size_t n) {
        actual = libdeflate_gzip_compress(compressor, input.data(), input.size(), p, n);
        return actual;
    });
    return actual != 0;
}

inline bool deflate_compress(std::string_view input, std::string& output, int level) {
    if (input.empty()) return false;
    auto* compressor = tls_libdeflate_pool.get(level);
    if (!compressor) return false;

    size_t bound = libdeflate_zlib_compress_bound(compressor, input.size());
    size_t actual = 0;
    output.resize_and_overwrite(bound, [&](char* p, size_t n) {
        actual = libdeflate_zlib_compress(compressor, input.data(), input.size(), p, n);
        return actual;
    });
    return actual != 0;
}

// Per-thread bump allocator for the brotli encoder. A BrotliEncoderState allocates its
// hash tables on creation and frees them on destruction; with this arena both are a
// pointer bump and a reset, so compressing a response does not touch malloc. The arena
// grows to the largest footprint seen (bounded by quality/lgwin/size hint) and stays there.
struct BrotliArena {
    std::unique_ptr<std::byte[]> block;
    size_t cap{0};
    size_t used{0};
    size_t peak{0};
    std::vector<void*> spill;  // allocations that did not fit, freed on reset()

    static void* alloc(void* opaque, size_t size) noexcept {
        auto* a = static_cast<BrotliArena*>(opaque);
        size = (size + 63) & ~size_t{63};
        a->peak = a->peak > a->used + size ? a->peak : a->used + size;
        if (a->used + size <= a->cap) {
            void* p = a->block.get() + a->used;
            a->used += size;
            return p;
        }
        a->used += size;  // counted, so the next reset() sizes the block to fit
        void* p = std::aligned_alloc(64, size);
        if (p) a->spill.push_back(p);
        return p;
    }

    static void free(void*, void*) noexcept {}  // released wholesale by reset()

    void reset() noexcept {
        for (void* p : spill) std::free(p);
        spill.clear();
        if (peak > cap) {
            size_t want = peak + peak / 4;
            block.reset(new (std::align_val_t{64}, std::nothrow) std::byte[want]);
            cap = block ? want : 0;
        }
        used = 0;
        peak = 0;
    }
};

inline thread_local BrotliArena tls_brotli_arena;

inline bool brotli_compress(std::string_view input, std::string& output, int quality, int lgwin) {
    if (input.empty()) return false;
    auto& arena = tls_brotli_arena;
    arena.reset();
    BrotliEncoderState* st = BrotliEncoderCreateInstance(&BrotliArena::alloc, &BrotliArena::free, &arena);
    if (!st) return false;
    BrotliEncoderSetParameter(st, BROTLI_PARAM_QUALITY, static_cast<uint32_t>(quality));
    BrotliEncoderSetParameter(st, BROTLI_PARAM_LGWIN, static_cast<uint32_t>(lgwin));
    BrotliEncoderSetParameter(st, BROTLI_PARAM_MODE, BROTLI_MODE_TEXT);
    BrotliEncoderSetParameter(st, BROTLI_PARAM_SIZE_HINT,
                              static_cast<uint32_t>(std::min<size_t>(input.size(), 1u << 30)));

    size_t bound = BrotliEncoderMaxCompressedSize(input.size());
    if (bound == 0) bound = input.size() + 1024;
    bool ok = false;
    output.resize_and_overwrite(bound, [&](char* p, size_t n) {
        size_t avail_in = input.size();
        const uint8_t* next_in = reinterpret_cast<const uint8_t*>(input.data());
        size_t avail_out = n;
        uint8_t* next_out = reinterpret_cast<uint8_t*>(p);
        ok = BrotliEncoderCompressStream(st, BROTLI_OPERATION_FINISH, &avail_in, &next_in,
                                         &avail_out, &next_out, nullptr)
             && BrotliEncoderIsFinished(st);
        return ok ? n - avail_out : 0;
    });
    BrotliEncoderDestroyInstance(st);
    return ok;
}

// Accept-Encoding (RFC 9110 12.5.3): the q-value of each coding this middleware can
// produce. -1 = not mentioned, 0 = refused. "*" covers codings not named explicitly.
struct AcceptedCodings {
    float br{-1}, gzip{-1}, deflate{-1}, star{-1};

    [[nodiscard]] float q(float named) const noexcept {
        if (named >= 0) return named;
        return star >= 0 ? star : 0.0f;
    }
};

inline bool iequals_ascii(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        unsigned char x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) return false;
    }
    return true;
}

inline AcceptedCodings parse_accept_encoding(std::string_view v) noexcept {
    AcceptedCodings out;
    auto trim = [](std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
        return s;
    };
    while (!v.empty()) {
        size_t comma = v.find(',');
        std::string_view item = v.substr(0, comma);
        v = comma == std::string_view::npos ? std::string_view{} : v.substr(comma + 1);
        size_t semi = item.find(';');
        std::string_view name = trim(item.substr(0, semi));
        float q = 1.0f;
        while (semi != std::string_view::npos) {
            std::string_view params = item.substr(semi + 1);
            size_t next = params.find(';');
            std::string_view param = trim(params.substr(0, next));
            if (param.size() >= 2 && (param[0] == 'q' || param[0] == 'Q') && param[1] == '=') {
                q = std::strtof(std::string(param.substr(2)).c_str(), nullptr);
            }
            semi = next == std::string_view::npos ? next : semi + 1 + next;
        }
        if (iequals_ascii(name, "br")) out.br = q;
        else if (iequals_ascii(name, "gzip") || iequals_ascii(name, "x-gzip")) out.gzip = q;
        else if (iequals_ascii(name, "deflate")) out.deflate = q;
        else if (name == "*") out.star = q;
    }
    return out;
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
namespace detail {
// Compresses the response in place if the client accepts an enabled coding. Runs after the
// handler; shared by the async and the synchronous form of the middleware.
inline void apply_compression(Context& ctx, const CompressOptions& options) {

        // 1. Skip if client did not send Accept-Encoding
        auto accept_encoding_opt = ctx.req().headers().get("accept-encoding");
        if (!accept_encoding_opt) {
            return;
        }
        std::string_view accept_encoding = *accept_encoding_opt;

        // 2. Skip if already encoded (e.g. static pre-compressed assets or manual encoder)
        if (ctx.res().headers().contains("content-encoding")) {
            return;
        }

        // 3. Skip if status code indicates no response body
        auto status = ctx.res().status();
        if (status == StatusCode::NoContent || status == StatusCode::NotModified) {
            return;
        }

        // 4. Skip if body is smaller than min_size threshold
        auto body = ctx.res().body();
        if (body.size() < options.min_size) {
            return;
        }

        // 5. Pick the coding the client prefers (q-values), ties broken br > gzip/deflate.
        const auto acc = detail::parse_accept_encoding(accept_encoding);
        enum class Coding { None, Br, Gzip, Deflate };
        Coding pick = Coding::None;
        float best = 0.0f;
        auto consider = [&](Coding c, bool enabled, float q) {
            if (enabled && q > best) { best = q; pick = c; }
        };
        consider(Coding::Br, options.brotli, acc.q(acc.br));
        if (options.prefer_deflate) {
            consider(Coding::Deflate, options.deflate, acc.q(acc.deflate));
            consider(Coding::Gzip, options.gzip, acc.q(acc.gzip));
        } else {
            consider(Coding::Gzip, options.gzip, acc.q(acc.gzip));
            consider(Coding::Deflate, options.deflate, acc.q(acc.deflate));
        }

        bool compressed = false;
        auto& out = detail::tls_compress_scratch;
        switch (pick) {
            case Coding::Br:
                compressed = detail::brotli_compress(body, out, options.brotli_quality, options.brotli_lgwin);
                if (compressed) ctx.res().header("Content-Encoding", "br");
                break;
            case Coding::Gzip:
                compressed = detail::gzip_compress(body, out, options.level);
                if (compressed) ctx.res().header("Content-Encoding", "gzip");
                break;
            case Coding::Deflate:
                compressed = detail::deflate_compress(body, out, options.level);
                if (compressed) ctx.res().header("Content-Encoding", "deflate");
                break;
            case Coding::None:
                break;
        }
        if (compressed) {
            ctx.res().body(out);
        }

        // 6. Safely merge Vary header
        if (compressed && options.vary) {
            detail::merge_vary(ctx.res());
        }
}
} // namespace detail

inline MiddlewareFn Compress(CompressOptions options = {}) {
    return MiddlewareFn::with_sync(
        [options](Context& ctx, Next next) -> core::Task<void> {
            co_await next(ctx);
            detail::apply_compression(ctx, options);
        },
        [options](Context& ctx, SyncNext next) {
            next(ctx);
            detail::apply_compression(ctx, options);
        });
}

} // namespace aegon::http::middleware
