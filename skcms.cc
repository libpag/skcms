/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "skcms.h"
#include "skcms_internal.h"
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace gfx {

#if defined(__ARM_NEON)
    #include <arm_neon.h>
#elif defined(__SSE__)
    #include <immintrin.h>
#endif

static const union {
    uint32_t bits;
    float    f;
} inf_ = { 0x7f800000 };
#define INFINITY_ inf_.f

// Maps to an in-memory profile so that fields line up to the locations specified
// in ICC.1:2010, section 7.2

typedef struct {
    uint8_t signature [4];
    uint8_t offset    [4];
    uint8_t size      [4];
} tag_Layout;

const skcms_ICCProfile* cms_sRGB_profile() {
    static const skcms_ICCProfile sRGB_profile = {
        nullptr,               // buffer, moot here

        0,                     // size, moot here
        skcms_Signature_RGB,   // data_color_space
        skcms_Signature_XYZ,   // pcs
        0,                     // tag count, moot here

        // We choose to represent sRGB with its canonical transfer function,
        // and with its canonical XYZD50 gamut matrix.
        true,  // has_trc, followed by the 3 trc curves
        {
            {{0, {2.4f, (float)(1/1.055), (float)(0.055/1.055), (float)(1/12.92), 0.04045f, 0, 0}}},
            {{0, {2.4f, (float)(1/1.055), (float)(0.055/1.055), (float)(1/12.92), 0.04045f, 0, 0}}},
            {{0, {2.4f, (float)(1/1.055), (float)(0.055/1.055), (float)(1/12.92), 0.04045f, 0, 0}}},
        },

        true,  // has_toXYZD50, followed by 3x3 toXYZD50 matrix
        {{
            { 0.436065674f, 0.385147095f, 0.143066406f },
            { 0.222488403f, 0.716873169f, 0.060607910f },
            { 0.013916016f, 0.097076416f, 0.714096069f },
        }},

        false, // has_A2B, followed by a2b itself which we don't care about.
        {
            0,
            {
                {{0, {0,0, 0,0,0,0,0}}},
                {{0, {0,0, 0,0,0,0,0}}},
                {{0, {0,0, 0,0,0,0,0}}},
                {{0, {0,0, 0,0,0,0,0}}},
            },
            {0,0,0,0},
            nullptr,
            nullptr,

            0,
            {
                {{0, {0,0, 0,0,0,0,0}}},
                {{0, {0,0, 0,0,0,0,0}}},
                {{0, {0,0, 0,0,0,0,0}}},
            },
            {{
                { 0,0,0,0 },
                { 0,0,0,0 },
                { 0,0,0,0 },
            }},

            0,
            {
                {{0, {0,0, 0,0,0,0,0}}},
                {{0, {0,0, 0,0,0,0,0}}},
                {{0, {0,0, 0,0,0,0,0}}},
            },
        },
    };
    return &sRGB_profile;
}

#if defined(__clang__) || defined(__GNUC__)
    #define small_memcpy __builtin_memcpy
#else
    #define small_memcpy memcpy
#endif

// ~~~~ Impl. of skcmsTransform() ~~~~

typedef enum {
    Op_load_a8,
    Op_load_g8,
    Op_load_8888,

    Op_swap_rb,
    Op_clamp,
    Op_invert,
    Op_force_opaque,
    Op_premul,
    Op_unpremul,
    Op_matrix_3x3,
    Op_matrix_3x4,
    Op_lab_to_xyz,

    Op_tf_r,
    Op_tf_g,
    Op_tf_b,
    Op_tf_a,

    Op_table_r,
    Op_table_g,
    Op_table_b,
    Op_table_a,

    Op_clut,

    Op_store_a8,
    Op_store_g8,
    Op_store_8888,
} Op;

#if defined(__clang__)
    template <int N, typename T> using Vec = T __attribute__((ext_vector_type(N)));
#elif defined(__GNUC__)
    // For some reason GCC accepts this nonsense, but not the more straightforward version,
    //   template <int N, typename T> using Vec = T __attribute__((vector_size(N*sizeof(T))));
    template <int N, typename T>
    struct VecHelper { typedef T __attribute__((vector_size(N*sizeof(T)))) V; };

    template <int N, typename T> using Vec = typename VecHelper<N,T>::V;
#endif

// First, instantiate our default exec_ops() implementation using the default compiliation target.

namespace baseline {
#if defined(SKCMS_PORTABLE) || !(defined(__clang__) || defined(__GNUC__)) \
                            || (defined(__EMSCRIPTEN_major__) && !defined(__wasm_simd128__))
    #define N 1
    using F   = float;
    using U64 = uint64_t;
    using U32 = uint32_t;
    using I32 = int32_t;
    using U16 = uint16_t;
    using U8  = uint8_t;

#elif defined(__AVX512F__)
    #define N 16
    using   F = Vec<N,float>;
    using I32 = Vec<N,int32_t>;
    using U64 = Vec<N,uint64_t>;
    using U32 = Vec<N,uint32_t>;
    using U16 = Vec<N,uint16_t>;
    using  U8 = Vec<N,uint8_t>;
#elif defined(__AVX__)
    #define N 8
    using   F = Vec<N,float>;
    using I32 = Vec<N,int32_t>;
    using U64 = Vec<N,uint64_t>;
    using U32 = Vec<N,uint32_t>;
    using U16 = Vec<N,uint16_t>;
    using  U8 = Vec<N,uint8_t>;
#else
    #define N 4
    using   F = Vec<N,float>;
    using I32 = Vec<N,int32_t>;
    using U64 = Vec<N,uint64_t>;
    using U32 = Vec<N,uint32_t>;
    using U16 = Vec<N,uint16_t>;
    using  U8 = Vec<N,uint8_t>;
#endif

    #include "src/Transform_inl.h"
    #undef N
}

// Now, instantiate any other versions of run_program() we may want for runtime detection.
#if !defined(SKCMS_PORTABLE) &&                           \
        (( defined(__clang__) && __clang_major__ >= 5) || \
         (!defined(__clang__) && defined(__GNUC__)))      \
     && defined(__x86_64__) && !defined(__AVX2__)

    #if defined(__clang__)
        #pragma clang attribute push(__attribute__((target("avx2,f16c"))), apply_to=function)
    #elif defined(__GNUC__)
        #pragma GCC push_options
        #pragma GCC target("avx2,f16c")
    #endif

    namespace hsw {
        #define USING_AVX
        #define USING_AVX_F16C
        #define USING_AVX2
        #define N 8
        using   F = Vec<N,float>;
        using I32 = Vec<N,int32_t>;
        using U64 = Vec<N,uint64_t>;
        using U32 = Vec<N,uint32_t>;
        using U16 = Vec<N,uint16_t>;
        using  U8 = Vec<N,uint8_t>;

        #include "src/Transform_inl.h"

        // src/Transform_inl.h will undefine USING_* for us.
        #undef N
    }

    #if defined(__clang__)
        #pragma clang attribute pop
    #elif defined(__GNUC__)
        #pragma GCC pop_options
    #endif

    #define TEST_FOR_HSW

    static bool hsw_ok() {
        static const bool ok = []{
            // See http://www.sandpile.org/x86/cpuid.htm

            // First, a basic cpuid(1).
            uint32_t eax, ebx, ecx, edx;
            __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                                         : "0"(1), "2"(0));

            // Sanity check for prerequisites.
            if ((edx & (1<<25)) != (1<<25)) { return false; }   // SSE
            if ((edx & (1<<26)) != (1<<26)) { return false; }   // SSE2
            if ((ecx & (1<< 0)) != (1<< 0)) { return false; }   // SSE3
            if ((ecx & (1<< 9)) != (1<< 9)) { return false; }   // SSSE3
            if ((ecx & (1<<19)) != (1<<19)) { return false; }   // SSE4.1
            if ((ecx & (1<<20)) != (1<<20)) { return false; }   // SSE4.2

            if ((ecx & (3<<26)) != (3<<26)) { return false; }   // XSAVE + OSXSAVE

            {
                uint32_t eax_xgetbv, edx_xgetbv;
                __asm__ __volatile__("xgetbv" : "=a"(eax_xgetbv), "=d"(edx_xgetbv) : "c"(0));
                if ((eax_xgetbv & (3<<1)) != (3<<1)) { return false; }  // XMM+YMM state saved?
            }

            if ((ecx & (1<<28)) != (1<<28)) { return false; }   // AVX
            if ((ecx & (1<<29)) != (1<<29)) { return false; }   // F16C
            if ((ecx & (1<<12)) != (1<<12)) { return false; }   // FMA  (TODO: not currently used)

            // Call cpuid(7) to check for our final AVX2 feature bit!
            __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                                         : "0"(7), "2"(0));
            if ((ebx & (1<< 5)) != (1<< 5)) { return false; }   // AVX2

            return true;
        }();

        return ok;
    }

#endif

static size_t bytes_per_pixel(skcms_PixelFormat fmt) {
    switch (fmt >> 1) {   // ignore rgb/bgr
        case skcms_PixelFormat_A_8                >> 1: return  1;
        case skcms_PixelFormat_RGBA_8888          >> 1: return  4;
    }
    assert(false);
    return 0;
}

bool skcmsTransform(const void*             src,
                     skcms_PixelFormat       srcFmt,
                     skcms_AlphaFormat       srcAlpha,
                     const skcms_ICCProfile* srcProfile,
                     void*                   dst,
                     skcms_PixelFormat       dstFmt,
                     skcms_AlphaFormat       dstAlpha,
                     const skcms_ICCProfile* dstProfile,
                     size_t                  npixels) {
    return skcmsTransformWithPalette(src, srcFmt, srcAlpha, srcProfile,
                                      dst, dstFmt, dstAlpha, dstProfile,
                                      npixels, nullptr);
}

bool skcmsTransformWithPalette(const void*             src,
                                skcms_PixelFormat       srcFmt,
                                skcms_AlphaFormat       srcAlpha,
                                const skcms_ICCProfile* srcProfile,
                                void*                   dst,
                                skcms_PixelFormat       dstFmt,
                                skcms_AlphaFormat       dstAlpha,
                                const skcms_ICCProfile* dstProfile,
                                size_t                  nz,
                                const void*             ) {
    const size_t dst_bpp = bytes_per_pixel(dstFmt),
                 src_bpp = bytes_per_pixel(srcFmt);
    // Let's just refuse if the request is absurdly big.
    if (nz * dst_bpp > INT_MAX || nz * src_bpp > INT_MAX) {
        return false;
    }
    int n = (int)nz;

    // Null profiles default to sRGB. Passing null for both is handy when doing format conversion.
    if (!srcProfile) {
        srcProfile = cms_sRGB_profile();
    }
    if (!dstProfile) {
        dstProfile = cms_sRGB_profile();
    }

    // We can't transform in place unless the PixelFormats are the same size.
    if (dst == src && dst_bpp != src_bpp) {
        return false;
    }
    // TODO: more careful alias rejection (like, dst == src + 1)?

    Op          program  [32];
    const void* arguments[32];

    Op*          ops  = program;

    switch (srcFmt >> 1) {
        default: return false;
        case skcms_PixelFormat_A_8             >> 1: *ops++ = Op_load_a8;         break;
        case skcms_PixelFormat_RGBA_8888       >> 1: *ops++ = Op_load_8888;       break;
    }
    if (srcFmt & 1) {
        *ops++ = Op_swap_rb;
    }

    if (srcAlpha == skcms_AlphaFormat_Opaque) {
        *ops++ = Op_force_opaque;
    } else if (srcAlpha == skcms_AlphaFormat_PremulAsEncoded) {
        *ops++ = Op_unpremul;
    }

    *ops++ = Op_clamp;
    if (dstAlpha == skcms_AlphaFormat_Opaque) {
        *ops++ = Op_force_opaque;
    } else if (dstAlpha == skcms_AlphaFormat_PremulAsEncoded) {
        *ops++ = Op_premul;
    }
    if (dstFmt & 1) {
        *ops++ = Op_swap_rb;
    }
    switch (dstFmt >> 1) {
        default: return false;
        case skcms_PixelFormat_A_8             >> 1: *ops++ = Op_store_a8;         break;
        case skcms_PixelFormat_RGBA_8888       >> 1: *ops++ = Op_store_8888;       break;
    }

    auto run = baseline::run_program;
#if defined(TEST_FOR_HSW)
    if (hsw_ok()) { run = hsw::run_program; }
#endif
    run(program, arguments, (const char*)src, (char*)dst, n, src_bpp,dst_bpp);
    return true;
}

}  // namespace gfx

