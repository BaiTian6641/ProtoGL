/**
 * @file PglShaderBytecode.h
 * @brief PGL Shader Bytecode (PSB) format definitions — shared between host and GPU.
 *
 * Defines the binary format for compiled PGLSL shaders:
 *   - Program header (16 bytes)
 *   - Uniform descriptor table
 *   - Constants pool
 *   - Instruction stream (4-byte fixed-width)
 *
 * See docs/Shader_System_Design.md §4 for full specification.
 *
 * ProtoGL API Specification v0.6
 */

#pragma once

#include <cstdint>

// ─── PSB Magic Number ───────────────────────────────────────────────────────

static constexpr uint32_t PSB_MAGIC = 0x50534231;  // "PSB1" in ASCII
static constexpr uint8_t  PSB_VERSION = 1;

// ─── Program Limits ─────────────────────────────────────────────────────────

static constexpr uint8_t  PSB_MAX_UNIFORMS    = 16;
static constexpr uint8_t  PSB_MAX_CONSTANTS   = 32;
static constexpr uint16_t PSB_MAX_INSTRUCTIONS = 256;
static constexpr uint8_t  PSB_NUM_REGISTERS   = 32;

// Max program binary size: header(16) + uniforms(16*8) + constants(32*4) + instructions(256*4)
static constexpr uint16_t PSB_MAX_PROGRAM_SIZE = 1296;

// ─── Program Header Flags ───────────────────────────────────────────────────

static constexpr uint8_t PSB_FLAG_NEEDS_SCRATCH_COPY = 0x01;  // Shader reads from framebuffer (needs scratch copy)

// ─── Program Header (16 bytes) ──────────────────────────────────────────────

struct PglShaderProgramHeader {
    uint32_t magic;          // Must be PSB_MAGIC (0x50534231)
    uint8_t  version;        // PSB_VERSION (1)
    uint8_t  flags;          // PSB_FLAG_* bits
    uint8_t  constCount;     // Number of constants (0–32)
    uint8_t  uniformCount;   // Number of uniforms (0–16)
    uint16_t instrCount;     // Number of instructions (0–256)
    uint32_t nameHash;       // FNV-1a hash of shader source (for debug identification)
    uint16_t reserved;       // Must be 0
};
static_assert(sizeof(PglShaderProgramHeader) == 16, "PglShaderProgramHeader must be 16 bytes");

// ─── Uniform Descriptor (8 bytes each) ──────────────────────────────────────

enum PglUniformType : uint8_t {
    PSB_UNIFORM_FLOAT = 0,   // 1 component
    PSB_UNIFORM_VEC2  = 1,   // 2 components
    PSB_UNIFORM_VEC3  = 2,   // 3 components
    PSB_UNIFORM_VEC4  = 3,   // 4 components
};

struct PglUniformDescriptor {
    uint32_t nameHash;           // FNV-1a hash of uniform name
    uint8_t  type;               // PglUniformType
    uint8_t  slot;               // Uniform index 0–15
    uint16_t defaultValueOffset; // Byte offset into constants pool for default value
};
static_assert(sizeof(PglUniformDescriptor) == 8, "PglUniformDescriptor must be 8 bytes");

// ─── Operand Encoding (8-bit) ───────────────────────────────────────────────
//
// Each instruction operand (dst, srcA, srcB) is encoded as:
//   0x00–0x1F → Register r0–r31
//   0x20–0x2F → Uniform u0–u15
//   0x30–0x4F → Constant pool c0–c31
//   0x50–0x5F → Literal small constant (see table below)
//   0xFF      → Unused / ignored

static constexpr uint8_t PSB_OP_REG_BASE      = 0x00;  // r0
static constexpr uint8_t PSB_OP_REG_END        = 0x1F;  // r31
static constexpr uint8_t PSB_OP_UNIFORM_BASE   = 0x20;  // u0
static constexpr uint8_t PSB_OP_UNIFORM_END    = 0x2F;  // u15
static constexpr uint8_t PSB_OP_CONST_BASE     = 0x30;  // c0
static constexpr uint8_t PSB_OP_CONST_END      = 0x4F;  // c31
static constexpr uint8_t PSB_OP_LITERAL_BASE   = 0x50;  // literal 0
static constexpr uint8_t PSB_OP_LITERAL_END    = 0x5F;  // literal 15
static constexpr uint8_t PSB_OP_UNUSED         = 0xFF;

// ─── Literal Small Constants ────────────────────────────────────────────────
// Encoded inline in operand byte, no pool lookup needed.

static constexpr float PSB_LITERALS[] = {
     0.0f,     // 0x50
     0.5f,     // 0x51
     1.0f,     // 0x52
     2.0f,     // 0x53
    -1.0f,     // 0x54
    -0.5f,     // 0x55
     3.14159265f, // 0x56  (π)
     6.28318530f, // 0x57  (2π)
     0.15915494f, // 0x58  (1/2π)
     0.31830989f, // 0x59  (1/π)
     0.01f,    // 0x5A
     0.1f,     // 0x5B
    10.0f,     // 0x5C
   100.0f,     // 0x5D
     0.70710678f, // 0x5E  (1/√2)
     1.41421356f, // 0x5F  (√2)
};
static constexpr uint8_t PSB_LITERAL_COUNT = sizeof(PSB_LITERALS) / sizeof(PSB_LITERALS[0]);

// ─── Instruction Encoding ───────────────────────────────────────────────────
// Each instruction is 4 bytes: [opcode][dst][srcA][srcB]

struct PglShaderInstruction {
    uint8_t opcode;
    uint8_t dst;
    uint8_t srcA;
    uint8_t srcB;
};
static_assert(sizeof(PglShaderInstruction) == 4, "PglShaderInstruction must be 4 bytes");

// ─── Opcode Definitions (Shader VM Instructions) ────────────────────────────
//
// Categories:
//   0x00      — NOP / special
//   0x01–0x07 — Arithmetic
//   0x10–0x21 — Math functions
//   0x30–0x35 — Clamping / interpolation
//   0x40–0x47 — Geometric (vector)
//   0x50      — Texture sampling
//   0x60–0x61 — Load
//   0xFF      — END (halt)

// Special
static constexpr uint8_t PSB_OP_NOP     = 0x00;  // No operation
static constexpr uint8_t PSB_OP_END     = 0xFF;  // Halt, output r28-r31

// Arithmetic (dst = srcA op srcB)
static constexpr uint8_t PSB_OP_MOV     = 0x01;  // dst = srcA
static constexpr uint8_t PSB_OP_ADD     = 0x02;  // dst = srcA + srcB
static constexpr uint8_t PSB_OP_SUB     = 0x03;  // dst = srcA - srcB
static constexpr uint8_t PSB_OP_MUL     = 0x04;  // dst = srcA * srcB
static constexpr uint8_t PSB_OP_DIV     = 0x05;  // dst = srcA / srcB (safe: 0 if srcB=0)
static constexpr uint8_t PSB_OP_FMA     = 0x06;  // dst = srcA * srcB + dst
static constexpr uint8_t PSB_OP_NEG     = 0x07;  // dst = -srcA

// Math functions (dst = fn(srcA) or fn(srcA, srcB))
static constexpr uint8_t PSB_OP_SIN     = 0x10;  // dst = sin(srcA)
static constexpr uint8_t PSB_OP_COS     = 0x11;  // dst = cos(srcA)
static constexpr uint8_t PSB_OP_TAN     = 0x12;  // dst = tan(srcA)
static constexpr uint8_t PSB_OP_ASIN    = 0x13;  // dst = asin(srcA)
static constexpr uint8_t PSB_OP_ACOS    = 0x14;  // dst = acos(srcA)
static constexpr uint8_t PSB_OP_ATAN    = 0x15;  // dst = atan(srcA)
static constexpr uint8_t PSB_OP_ATAN2   = 0x16;  // dst = atan2(srcA, srcB)
static constexpr uint8_t PSB_OP_POW     = 0x17;  // dst = pow(srcA, srcB)
static constexpr uint8_t PSB_OP_EXP     = 0x18;  // dst = exp(srcA)
static constexpr uint8_t PSB_OP_LOG     = 0x19;  // dst = log(srcA)
static constexpr uint8_t PSB_OP_SQRT    = 0x1A;  // dst = sqrt(srcA)
static constexpr uint8_t PSB_OP_RSQRT   = 0x1B;  // dst = 1/sqrt(srcA)
static constexpr uint8_t PSB_OP_ABS     = 0x1C;  // dst = abs(srcA)
static constexpr uint8_t PSB_OP_SIGN    = 0x1D;  // dst = sign(srcA) → -1, 0, or 1
static constexpr uint8_t PSB_OP_FLOOR   = 0x1E;  // dst = floor(srcA)
static constexpr uint8_t PSB_OP_CEIL    = 0x1F;  // dst = ceil(srcA)
static constexpr uint8_t PSB_OP_FRACT   = 0x20;  // dst = fract(srcA) = srcA - floor(srcA)
static constexpr uint8_t PSB_OP_MOD     = 0x21;  // dst = mod(srcA, srcB)

// Clamping / interpolation
static constexpr uint8_t PSB_OP_MIN     = 0x30;  // dst = min(srcA, srcB)
static constexpr uint8_t PSB_OP_MAX     = 0x31;  // dst = max(srcA, srcB)
static constexpr uint8_t PSB_OP_CLAMP   = 0x32;  // dst = clamp(srcA, srcB, dst)  [3-op: lo=srcB, hi=dst]
static constexpr uint8_t PSB_OP_MIX     = 0x33;  // dst = mix(srcA, srcB, dst)    [3-op: t=dst]
static constexpr uint8_t PSB_OP_STEP    = 0x34;  // dst = step(srcA, srcB) → srcB>=srcA ? 1 : 0
static constexpr uint8_t PSB_OP_SSTEP   = 0x35;  // dst = smoothstep(srcA, srcB, dst) [3-op]

// Geometric (vector operations on consecutive registers)
static constexpr uint8_t PSB_OP_DOT2    = 0x40;  // dst = dot(srcA..+1, srcB..+1) — 2D
static constexpr uint8_t PSB_OP_DOT3    = 0x41;  // dst = dot(srcA..+2, srcB..+2) — 3D
static constexpr uint8_t PSB_OP_LEN2    = 0x42;  // dst = length(srcA, srcA+1)
static constexpr uint8_t PSB_OP_LEN3    = 0x43;  // dst = length(srcA..+2)
static constexpr uint8_t PSB_OP_NORM2   = 0x44;  // dst,dst+1 = normalize(srcA, srcA+1)
static constexpr uint8_t PSB_OP_NORM3   = 0x45;  // dst..+2 = normalize(srcA..+2)
static constexpr uint8_t PSB_OP_CROSS   = 0x46;  // dst..+2 = cross(srcA..+2, srcB..+2)
static constexpr uint8_t PSB_OP_DIST2   = 0x47;  // dst = distance(srcA..+1, srcB..+1)

// Texture sampling
static constexpr uint8_t PSB_OP_TEX2D   = 0x50;  // dst..+3 = texture2D(fb, srcA, srcA+1) → RGBA

// Load
static constexpr uint8_t PSB_OP_LCONST  = 0x60;  // dst = constants[srcA] (srcA is pool index)
static constexpr uint8_t PSB_OP_LUNI    = 0x61;  // dst = uniforms[srcA]

// ─── Reserved Register Assignments (auto-loaded per pixel) ──────────────────
//
// r0  = gl_FragCoord.x
// r1  = gl_FragCoord.y
// r2  = 0.0 (z)
// r3  = 1.0 (w)
// r4  = current pixel R (0.0–1.0)
// r5  = current pixel G (0.0–1.0)
// r6  = current pixel B (0.0–1.0)
// r7  = 1.0 (alpha)
// r8–r27  = user temporaries (20 registers)
// r28 = output R (gl_FragColor.r)
// r29 = output G (gl_FragColor.g)
// r30 = output B (gl_FragColor.b)
// r31 = output A (gl_FragColor.a)

static constexpr uint8_t PSB_REG_FRAG_X     = 0;
static constexpr uint8_t PSB_REG_FRAG_Y     = 1;
static constexpr uint8_t PSB_REG_FRAG_Z     = 2;
static constexpr uint8_t PSB_REG_FRAG_W     = 3;
static constexpr uint8_t PSB_REG_IN_R       = 4;
static constexpr uint8_t PSB_REG_IN_G       = 5;
static constexpr uint8_t PSB_REG_IN_B       = 6;
static constexpr uint8_t PSB_REG_IN_A       = 7;
static constexpr uint8_t PSB_REG_USER_START = 8;
static constexpr uint8_t PSB_REG_USER_END   = 27;
static constexpr uint8_t PSB_REG_OUT_R      = 28;
static constexpr uint8_t PSB_REG_OUT_G      = 29;
static constexpr uint8_t PSB_REG_OUT_B      = 30;
static constexpr uint8_t PSB_REG_OUT_A      = 31;

// ─── Auto-Bound Uniform Slots ───────────────────────────────────────────────
// These are always populated by the VM before execution (not user-settable).
// User uniforms start at slot 2.

static constexpr uint8_t PSB_AUTO_UNIFORM_RESOLUTION_X = 0;  // u_resolution.x
static constexpr uint8_t PSB_AUTO_UNIFORM_RESOLUTION_Y = 1;  // u_resolution.y
static constexpr uint8_t PSB_AUTO_UNIFORM_TIME         = 2;  // u_time (elapsed seconds)
static constexpr uint8_t PSB_USER_UNIFORM_START        = 3;  // First user-assignable uniform

// ─── FNV-1a Hash (for uniform name lookup) ──────────────────────────────────

static inline uint32_t PsbFnv1a(const char* str) {
    uint32_t hash = 0x811C9DC5u;
    while (*str) {
        hash ^= static_cast<uint8_t>(*str++);
        hash *= 0x01000193u;
    }
    return hash;
}

// ─── Operand Resolution Helpers ─────────────────────────────────────────────

/// Resolve an 8-bit operand to a float value.
/// @param op         Operand byte
/// @param regs       Register file (32 floats)
/// @param uniforms   Uniform table (PSB_MAX_UNIFORMS floats)
/// @param constants  Constants pool (PSB_MAX_CONSTANTS floats)
static inline float PsbResolveOperand(uint8_t op,
                                       const float* regs,
                                       const float* uniforms,
                                       const float* constants) {
    if (op <= PSB_OP_REG_END)       return regs[op];
    if (op <= PSB_OP_UNIFORM_END)   return uniforms[op - PSB_OP_UNIFORM_BASE];
    if (op <= PSB_OP_CONST_END)     return constants[op - PSB_OP_CONST_BASE];
    if (op <= PSB_OP_LITERAL_END)   return PSB_LITERALS[op - PSB_OP_LITERAL_BASE];
    return 0.0f;  // PSB_OP_UNUSED or invalid
}

