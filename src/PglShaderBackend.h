/**
 * @file PglShaderBackend.h
 * @brief Platform-portable shader math backend — compile-time dispatch.
 *
 * Provides all mathematical, geometric, texture-sampling, and pixel-packing
 * operations used by the shader VM and screenspace effect pipeline.  Each
 * function is static inline so the compiler can inline them directly into
 * the interpreter loop with zero call overhead.
 *
 * Backend selection (compile-time flags — set by build system or gpu_config.h):
 *
 *   PGL_BACKEND_SCALAR_FLOAT  (default, always available)
 *     — Standard C <cmath> functions.  Works on any platform.
 *
 *   PGL_BACKEND_CM33_FPV5
 *     — Cortex-M33 FPv5: fmaf is single-cycle, __builtin_sqrtf → VSQRT.
 *       Auto-detected via __ARM_FEATURE_FMA.
 *
 *   PGL_BACKEND_SOFT_FLOAT
 *     — Integer-only approximate math for cores without FPU (e.g. RISC-V
 *       Hazard3 in integer mode).  Trades 1–2 LSB precision for speed.
 *
 * The scalar-float backend is the *reference implementation*.  All other
 * backends must produce bit-identical or near-identical results (within
 * float32 rounding tolerance).
 *
 * Compile-location policy: This header lives in lib/ProtoGL/src/ (shared)
 * and is used by both the GPU firmware VM and optional host-side simulation.
 * The PGLSL compiler (PglShaderCompiler.h) does NOT depend on this file —
 * it emits bytecode only.
 */

#pragma once

#include <cstdint>

// ─── Auto-detect platform capabilities ──────────────────────────────────────

// Cortex-M33 FPv5 — single-cycle FMA, hardware VSQRT
#if defined(__ARM_FEATURE_FMA) || defined(__ARM_FP)
    #ifndef PGL_BACKEND_CM33_FPV5
        #define PGL_BACKEND_CM33_FPV5 1
    #endif
#endif

// Cortex-M33 DSP — saturating half-word ops for pixel blending
#if defined(__ARM_FEATURE_DSP) || defined(__ARM_ARCH_8M_MAIN__)
    #ifndef PGL_BACKEND_CM33_DSP
        #define PGL_BACKEND_CM33_DSP 1
    #endif
#endif

// Default: scalar float (always available)
#ifndef PGL_BACKEND_SCALAR_FLOAT
    #define PGL_BACKEND_SCALAR_FLOAT 1
#endif

// ─── Include platform math ──────────────────────────────────────────────────

#if PGL_BACKEND_SOFT_FLOAT
    // Soft-float backend — avoid <cmath> overhead.  Approximations defined below.
#else
    #include <cmath>
#endif

#if PGL_BACKEND_CM33_DSP
    #if __has_include(<arm_compat.h>)
        #include <arm_compat.h>
    #endif
#endif

namespace PglShaderBackend {

// ═══════════════════════════════════════════════════════════════════════════
// ── ARITHMETIC ──────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

static inline float Add(float a, float b) { return a + b; }
static inline float Sub(float a, float b) { return a - b; }
static inline float Mul(float a, float b) { return a * b; }
static inline float Neg(float a) { return -a; }

static inline float Div(float a, float b) {
    return (b == 0.0f) ? 0.0f : (a / b);
}

static inline float Fma(float a, float b, float c) {
#if PGL_BACKEND_CM33_FPV5 || defined(__ARM_FEATURE_FMA)
    return __builtin_fmaf(a, b, c);
#elif PGL_BACKEND_SOFT_FLOAT
    return a * b + c;  // no fused, but avoids soft-fma overhead
#else
    return fmaf(a, b, c);
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// ── MATH FUNCTIONS ──────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

#if PGL_BACKEND_SOFT_FLOAT

// ─── Soft-float approximations (for integer-only cores) ─────────────────
// These use polynomial approximations (Bhaskara/Chebyshev) tuned for
// visual accuracy in shader effects rather than scientific precision.

// π constants
static constexpr float PI_F    = 3.14159265358979323846f;
static constexpr float TWO_PI  = 6.28318530717958647692f;
static constexpr float HALF_PI = 1.57079632679489661923f;

// Parabolic sin approximation (Bhaskara): max error ~0.001
static inline float Sin(float x) {
    // Normalise to [-π, π]
    x = x - TWO_PI * static_cast<int>(x / TWO_PI);
    if (x > PI_F) x -= TWO_PI;
    if (x < -PI_F) x += TWO_PI;
    // Bhaskara I: sin(x) ≈ 16x(π-x) / [5π² - 4x(π-x)]
    float xp = x * (PI_F - (x < 0 ? -x : x));
    return 4.0f * xp / (5.0f * PI_F * PI_F - xp * 0.8f);
}

static inline float Cos(float x) { return Sin(x + HALF_PI); }

static inline float Tan(float x) {
    float c = Cos(x);
    return (c == 0.0f) ? 0.0f : Sin(x) / c;
}

// Polynomial atan approximation (max error ~0.003 rad)
static inline float Atan(float x) {
    if (x > 1.0f) return HALF_PI - Atan(1.0f / x);
    if (x < -1.0f) return -HALF_PI - Atan(1.0f / x);
    float x2 = x * x;
    return x * (0.9998660f + x2 * (-0.3302995f + x2 * 0.1801410f));
}

static inline float Atan2(float y, float x) {
    if (x > 0.0f) return Atan(y / x);
    if (x < 0.0f) return Atan(y / x) + (y >= 0.0f ? PI_F : -PI_F);
    return (y > 0.0f) ? HALF_PI : ((y < 0.0f) ? -HALF_PI : 0.0f);
}

static inline float Asin(float x) {
    // Clamp input
    x = (x < -1.0f) ? -1.0f : ((x > 1.0f) ? 1.0f : x);
    return Atan2(x, 1.0f - x * x > 0.0f ? __builtin_sqrtf(1.0f - x * x) : 0.0f);
}

static inline float Acos(float x) { return HALF_PI - Asin(x); }

// Fast exp using Schraudolph's integer trick
static inline float Exp(float x) {
    // Clamp to avoid overflow
    if (x > 88.0f) x = 88.0f;
    if (x < -88.0f) return 0.0f;
    union { float f; int32_t i; } v;
    v.i = static_cast<int32_t>(12102203.0f * x + 1065353216.0f);
    return v.f;
}

static inline float Log(float x) {
    if (x <= 0.0f) return 0.0f;
    union { float f; int32_t i; } v;
    v.f = x;
    return (static_cast<float>(v.i) - 1065353216.0f) / 12102203.0f;
}

static inline float Pow(float base, float exp) {
    if (base <= 0.0f) return 0.0f;
    return Exp(exp * Log(base));
}

// Quake-style fast inverse sqrt adapted
static inline float Rsqrt(float a) {
    if (a <= 0.0f) return 0.0f;
    union { float f; int32_t i; } v;
    v.f = a;
    v.i = 0x5f3759df - (v.i >> 1);
    v.f = v.f * (1.5f - 0.5f * a * v.f * v.f);  // one Newton-Raphson step
    return v.f;
}

static inline float Sqrt(float a) {
    if (a <= 0.0f) return 0.0f;
    return a * Rsqrt(a);
}

#else  // Hardware float path (SCALAR_FLOAT or CM33_FPV5)

static inline float Sin(float x)       { return sinf(x); }
static inline float Cos(float x)       { return cosf(x); }
static inline float Tan(float x)       { return tanf(x); }
static inline float Asin(float x)      { return asinf(x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x)); }
static inline float Acos(float x)      { return acosf(x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x)); }
static inline float Atan(float x)      { return atanf(x); }
static inline float Atan2(float y, float x) { return atan2f(y, x); }
static inline float Pow(float base, float exp) { return powf(fabsf(base), exp); }
static inline float Exp(float x)       { return expf(x); }
static inline float Log(float x)       { return (x > 0.0f) ? logf(x) : 0.0f; }

static inline float Sqrt(float a) {
#if PGL_BACKEND_CM33_FPV5
    return (a < 0.0f) ? 0.0f : __builtin_sqrtf(a);
#else
    return (a < 0.0f) ? 0.0f : sqrtf(a);
#endif
}

static inline float Rsqrt(float a) {
    if (a <= 0.0f) return 0.0f;
#if PGL_BACKEND_CM33_FPV5
    float s = __builtin_sqrtf(a);
    return (s > 0.0f) ? (1.0f / s) : 0.0f;
#else
    float s = sqrtf(a);
    return (s > 0.0f) ? (1.0f / s) : 0.0f;
#endif
}

#endif  // PGL_BACKEND_SOFT_FLOAT

// ═══════════════════════════════════════════════════════════════════════════
// ── ROUNDING / VALUE MANIPULATION ───────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

#if PGL_BACKEND_SOFT_FLOAT

static inline float Abs(float x)   { return (x < 0.0f) ? -x : x; }
static inline float Floor(float x) { int i = static_cast<int>(x); return static_cast<float>(i - (static_cast<float>(i) > x)); }
static inline float Ceil(float x)  { int i = static_cast<int>(x); return static_cast<float>(i + (static_cast<float>(i) < x)); }

#else

static inline float Abs(float x)   { return fabsf(x); }
static inline float Floor(float x) { return floorf(x); }
static inline float Ceil(float x)  { return ceilf(x); }

#endif

static inline float Sign(float x) {
    if (x > 0.0f) return 1.0f;
    if (x < 0.0f) return -1.0f;
    return 0.0f;
}

static inline float Fract(float x) {
    return x - Floor(x);
}

static inline float Mod(float x, float y) {
    if (y == 0.0f) return 0.0f;
#if PGL_BACKEND_SOFT_FLOAT
    return x - Floor(x / y) * y;
#else
    return fmodf(x, y);
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// ── CLAMPING / INTERPOLATION ────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

static inline float Min(float a, float b) { return (a < b) ? a : b; }
static inline float Max(float a, float b) { return (a > b) ? a : b; }

static inline float Clamp(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline float Mix(float a, float b, float t) {
    return Fma(t, b - a, a);   // a + t*(b-a), using FMA if available
}

static inline float Step(float edge, float x) {
    return (x >= edge) ? 1.0f : 0.0f;
}

static inline float Smoothstep(float edge0, float edge1, float x) {
    if (edge0 >= edge1) return 0.0f;
    float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ═══════════════════════════════════════════════════════════════════════════
// ── GEOMETRIC (2D / 3D) ────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

static inline float Dot2(float ax, float ay, float bx, float by) {
    return Fma(ax, bx, ay * by);
}

static inline float Dot3(float ax, float ay, float az,
                          float bx, float by, float bz) {
    return Fma(ax, bx, Fma(ay, by, az * bz));
}

static inline float Len2(float x, float y) {
    return Sqrt(Fma(x, x, y * y));
}

static inline float Len3(float x, float y, float z) {
    return Sqrt(Fma(x, x, Fma(y, y, z * z)));
}

static inline void Norm2(float x, float y, float& outX, float& outY) {
    float len = Len2(x, y);
    float inv = (len > 0.0f) ? (1.0f / len) : 0.0f;
    outX = x * inv;
    outY = y * inv;
}

static inline void Norm3(float x, float y, float z,
                          float& outX, float& outY, float& outZ) {
    float len = Len3(x, y, z);
    float inv = (len > 0.0f) ? (1.0f / len) : 0.0f;
    outX = x * inv;
    outY = y * inv;
    outZ = z * inv;
}

static inline void Cross(float ax, float ay, float az,
                          float bx, float by, float bz,
                          float& outX, float& outY, float& outZ) {
    outX = Fma(ay, bz, -(az * by));
    outY = Fma(az, bx, -(ax * bz));
    outZ = Fma(ax, by, -(ay * bx));
}

static inline float Dist2(float ax, float ay, float bx, float by) {
    float dx = ax - bx, dy = ay - by;
    return Len2(dx, dy);
}

// ═══════════════════════════════════════════════════════════════════════════
// ── TEXTURE SAMPLING ────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

/// Unpack RGB565 pixel to float [0,1] per channel.
static inline void UnpackRGB565(uint16_t pixel, float& r, float& g, float& b) {
    r = static_cast<float>((pixel >> 11) & 0x1F) * (1.0f / 31.0f);
    g = static_cast<float>((pixel >>  5) & 0x3F) * (1.0f / 63.0f);
    b = static_cast<float>( pixel        & 0x1F) * (1.0f / 31.0f);
}

/// Pack float [0,1] RGB to RGB565.
static inline uint16_t PackRGB565(float r, float g, float b) {
    int ri = static_cast<int>(r * 31.0f + 0.5f);
    int gi = static_cast<int>(g * 63.0f + 0.5f);
    int bi = static_cast<int>(b * 31.0f + 0.5f);
    ri = ri < 0 ? 0 : (ri > 31 ? 31 : ri);
    gi = gi < 0 ? 0 : (gi > 63 ? 63 : gi);
    bi = bi < 0 ? 0 : (bi > 31 ? 31 : bi);
    return static_cast<uint16_t>((ri << 11) | (gi << 5) | bi);
}

/// Pack integer 5/6/5-bit channels to RGB565.
static inline uint16_t PackRGB565i(uint8_t r5, uint8_t g6, uint8_t b5) {
    return static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

/// Sample RGB565 framebuffer at UV [0,1] → float RGB (nearest-neighbour).
static inline void TexSample(const uint16_t* fb, uint16_t w, uint16_t h,
                              float u, float v,
                              float& outR, float& outG, float& outB) {
    int px = static_cast<int>(u * static_cast<float>(w));
    int py = static_cast<int>(v * static_cast<float>(h));
    px = (px < 0) ? 0 : (px >= w ? w - 1 : px);
    py = (py < 0) ? 0 : (py >= h ? h - 1 : py);
    UnpackRGB565(fb[py * w + px], outR, outG, outB);
}

/// Extract 5-bit red channel from RGB565.
static inline uint8_t R5(uint16_t c) { return static_cast<uint8_t>((c >> 11) & 0x1F); }
/// Extract 6-bit green channel from RGB565.
static inline uint8_t G6(uint16_t c) { return static_cast<uint8_t>((c >>  5) & 0x3F); }
/// Extract 5-bit blue channel from RGB565.
static inline uint8_t B5(uint16_t c) { return static_cast<uint8_t>( c        & 0x1F); }

/// Clamp integer to 5-bit range [0,31].
static inline uint8_t Clamp5(int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 31 ? 31 : v)); }
/// Clamp integer to 6-bit range [0,63].
static inline uint8_t Clamp6(int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 63 ? 63 : v)); }

}  // namespace PglShaderBackend
