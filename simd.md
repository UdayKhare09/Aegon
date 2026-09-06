# CPU Extensions and Features

## Hardware Overview

## 1. SIMD & Vector Instruction Sets

### AVX-512 (512-bit Vector Extensions)
- `avx512f`: AVX-512 Foundation (core 512-bit vector operations)
- `avx512dq`: Doubleword and Quadword instructions
- `avx512bw`: Byte and Word instructions
- `avx512cd`: Conflict Detection instructions
- `avx512vl`: Vector Length Extensions (allows AVX-512 instructions on 128-bit and 256-bit XMM/YMM registers)
- `avx512ifma`: Integer Fused Multiply-Add (52-bit precision)
- `avx512vbmi`: Vector Byte Manipulation Instructions
- `avx512_vbmi2`: Vector Byte Manipulation Instructions 2 (byte/word compress, expand, shifts)
- `avx512_vnni`: Vector Neural Network Instructions (INT8 / INT16 dot products for AI inference)
- `avx512_bitalg`: Bit Algorithms (bit count, shuffle per byte)
- `avx512_vpopcntdq`: Vector Population Count for Dwords and Qwords
- `avx512_bf16`: Brain Floating Point 16-bit instructions (BFLOAT16 matrix math/AI)

### AVX & AVX2 (256-bit Vector Extensions)
- `avx`: Advanced Vector Extensions (256-bit FP operations)
- `avx2`: Advanced Vector Extensions 2 (256-bit integer operations, gather, broadcast)
- `fma`: Fused Multiply-Add (3-operand FMA3: `a * b + c`)
- `f16c`: Half-precision float conversion (FP16 <-> FP32)

### SSE Generations (128-bit Vector Extensions)
- `sse`: Streaming SIMD Extensions
- `sse2`: Streaming SIMD Extensions 2
- `ssse3`: Supplemental Streaming SIMD Extensions 3 (palignr, phadd, etc.)
- `sse4_1`: Streaming SIMD Extensions 4.1 (blend, dpps, roundps, pextrd, etc.)
- `sse4_2`: Streaming SIMD Extensions 4.2 (CRC32, string comparisons, pcmpgtq)
- `sse4a`: AMD-specific SSE4a extensions (extrq, insertq, movntsd, movntss)
- `misalignsse`: Support for misaligned SSE operands without penalty

### Legacy SIMD
- `mmx`: MultiMedia eXtensions (64-bit integer SIMD)
- `mmxext`: AMD MMX Extensions
- `3dnowprefetch`: 3DNow! prefetch instructions (`prefetch`, `prefetchw`)

---

## 2. Cryptography & Hashing Extensions

- `aes`: AES New Instructions (hardware accelerated AES encryption/decryption)
- `vaes`: Vector AES (AES instructions expanded to 256-bit and 512-bit vector registers)
- `pclmulqdq`: Carry-Less Multiplication of Quadwords (used in GCM, CRC)
- `vpclmulqdq`: Vector Carry-Less Multiplication (256-bit and 512-bit PCLMULQDQ)
- `sha_ni`: SHA Extensions (hardware SHA-1 and SHA-256)
- `gfni`: Galois Field New Instructions (SIMD affine transformation / multiplication over GF(2^8))

---

## 3. Bit Manipulation & Arithmetic Extensions

- `bmi1`: Bit Manipulation Instruction Set 1 (`andn`, `bextr`, `blsi`, `blsmsk`, `blsr`, `tzcnt`)
- `bmi2`: Bit Manipulation Instruction Set 2 (`bzhi`, `mulx`, `pdep`, `pext`, `rorx`, `sarx`, `shlx`, `shrx`)
- `popcnt`: Population Count instruction (`POPCNT`)
- `abm`: Advanced Bit Manipulation (Leading Zero Count `lzcnt` + `popcnt`)
- `movbe`: Move Data After Swapping Bytes (hardware endian conversion)
- `adx`: Multi-Precision Add-Carry Instruction Extensions (`adcx`, `adox` for arbitrary-precision math)

---

## 4. Random Number Generation & Timers

- `rdrand`: Hardware on-chip true random number generator
- `rdseed`: Direct access to high-entropy hardware random seed generator (NIST SP 800-90B)
- `rdtscp`: Read Time-Stamp Counter and Processor ID (serialized timestamp counter)
- `rdpid`: Read Processor ID directly into a general purpose register without serialization overhead
- `constant_tsc`: Constant rate Time Stamp Counter across clock frequency changes
- `nonstop_tsc`: TSC does not stop in C-states

---

## 5. Memory Management, Cache Control & State Save

- `clflush`: Cache Line Flush
- `clflushopt`: Optimized Cache Line Flush (concurrent, weakly ordered)
- `clwb`: Cache Line Write Back (writes modified lines to memory without invalidating cache)
- `clzero`: Clear Zero Instruction (AMD extension to zero out an entire 64-byte cache line)
- `wbnoinvd`: Write Back and Do Not Invalidate Cache
- `fsgsbase`: Allows userspace read/write to FS/GS base registers (`rdfsbase`, `wrfsbase`, etc.)
- `invpcid`: Invalidate Process-Context Identifier (efficient TLB management)
- `pku` / `ospke`: Protection Keys for Userspace (page-level permission controls)
- `umip`: User-Mode Instruction Prevention
- `user_shstk`: User-space Shadow Stack (Control-flow Enforcement Technology / CET)
- `xsave`, `xsaveopt`, `xsavec`, `xsaves`: Extended Processor State Save/Restore
- `xgetbv1`: Extended Control Register query
- `erms`: Enhanced REP MOVSB / STOSB (fast string copy/fill in hardware)
- `fsrm`: Fast Short REP MOVSB (optimized short memory copies)