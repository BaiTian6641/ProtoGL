/**
 * @file PglTypes.h
 * @brief ProtoGL wire-format types and structures.
 *
 * These structs are designed for direct serialization (little-endian, packed).
 * They are shared between the ESP32-S3 host encoder and the RP2350 GPU decoder.
 *
 * ProtoGL API Specification v0.3 — FROZEN
 */

#pragma once

#include <cstdint>
#include <cstring>

// ─── Resource Handles ───────────────────────────────────────────────────────

using PglMesh     = uint16_t;
using PglMaterial = uint16_t;
using PglTexture  = uint16_t;
using PglCamera   = uint8_t;
using PglLayout   = uint8_t;

static constexpr PglMesh     PGL_INVALID_MESH     = 0xFFFF;
static constexpr PglMaterial PGL_INVALID_MATERIAL  = 0xFFFF;
static constexpr PglTexture  PGL_INVALID_TEXTURE   = 0xFFFF;
static constexpr PglCamera   PGL_INVALID_CAMERA    = 0xFF;
static constexpr PglLayout   PGL_INVALID_LAYOUT    = 0xFF;

// ─── Limits ─────────────────────────────────────────────────────────────────

static constexpr uint16_t PGL_MAX_MESHES     = 256;
static constexpr uint16_t PGL_MAX_MATERIALS   = 256;
static constexpr uint16_t PGL_MAX_TEXTURES    = 64;
static constexpr uint8_t  PGL_MAX_CAMERAS     = 4;
static constexpr uint8_t  PGL_MAX_LAYOUTS     = 8;
static constexpr uint8_t  PGL_MAX_DRAW_CALLS  = 64;

static constexpr uint16_t PGL_MAX_VERTICES    = 2048;
static constexpr uint16_t PGL_MAX_TRIANGLES   = 1024;

// ─── Wire Primitives (packed, little-endian) ────────────────────────────────

#pragma pack(push, 1)

struct PglVec2 {
    float x;
    float y;
};

struct PglVec3 {
    float x;
    float y;
    float z;
};

struct PglQuat {
    float w;
    float x;
    float y;
    float z;
};

struct PglColor3 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct PglIndex3 {
    uint16_t a;
    uint16_t b;
    uint16_t c;
};

// ─── Frame Header / Footer ──────────────────────────────────────────────────

static constexpr uint16_t PGL_SYNC_WORD = 0x55AA;

struct PglFrameHeader {
    uint16_t syncWord;      // must be PGL_SYNC_WORD
    uint32_t frameNumber;
    uint32_t totalLength;   // header + all commands + CRC16
    uint16_t commandCount;
};
static_assert(sizeof(PglFrameHeader) == 12, "PglFrameHeader must be 12 bytes");

struct PglFrameFooter {
    uint16_t crc16;
};

// ─── Command Header ─────────────────────────────────────────────────────────

struct PglCommandHeader {
    uint8_t  opcode;
    uint16_t payloadLength;  // bytes, excluding this 3-byte header
};
static_assert(sizeof(PglCommandHeader) == 3, "PglCommandHeader must be 3 bytes");

// ─── Transform (full, matching Transform.h) ─────────────────────────────────

struct PglTransform {
    PglVec3  position;
    PglQuat  rotation;
    PglVec3  scale;
    PglQuat  baseRotation;           // Transform::baseRotation (rotation * baseRotation)
    PglQuat  scaleRotationOffset;    // Transform::scaleRotationOffset
    PglVec3  scaleOffset;            // Transform::scaleOffset
    PglVec3  rotationOffset;         // Transform::rotationOffset
};
static_assert(sizeof(PglTransform) == 100, "PglTransform must be 100 bytes");

// ─── Command Payloads ───────────────────────────────────────────────────────

// CMD_BEGIN_FRAME (0x80)
struct PglCmdBeginFrame {
    uint32_t frameNumber;
    uint32_t frameTimeUs;   // microseconds since last frame
};
static_assert(sizeof(PglCmdBeginFrame) == 8, "PglCmdBeginFrame must be 8 bytes");

// CMD_END_FRAME (0x8F)
struct PglCmdEndFrame {
    uint32_t frameNumber;   // echo for verification
};

// CMD_DRAW_OBJECT (0x81), fixed portion only (variable vertex data follows if flagged)
struct PglCmdDrawObject {
    uint16_t meshId;
    uint16_t materialId;
    uint8_t  flags;         // bit0: enabled, bit1: hasVertexOverride
    PglVec3  position;
    PglQuat  rotation;
    PglVec3  scale;
    PglQuat  baseRotation;
    PglQuat  scaleRotationOffset;
    PglVec3  scaleOffset;
    PglVec3  rotationOffset;
};
static_assert(sizeof(PglCmdDrawObject) == 105, "PglCmdDrawObject must be 105 bytes");

enum PglDrawFlags : uint8_t {
    PGL_DRAW_ENABLED          = 0x01,
    PGL_DRAW_VERTEX_OVERRIDE  = 0x02,
};

// CMD_SET_CAMERA (0x82)
struct PglCmdSetCamera {
    uint8_t cameraId;
    uint8_t pixelLayoutId;
    PglVec3 position;
    PglQuat rotation;
    PglVec3 scale;
    PglQuat lookOffset;
    PglQuat baseRotation;   // CameraLayout::GetRotation() precomputed
    uint8_t is2D;
};
static_assert(sizeof(PglCmdSetCamera) == 75, "PglCmdSetCamera must be 75 bytes");

// ─── Screen-Space Shader System ─────────────────────────────────────────────
//
// General-purpose screen-space post-processing.  Instead of hardcoded effect
// types, the API exposes three shader *classes* — CONVOLUTION, DISPLACEMENT,
// COLOR_ADJUST — each parameterised to express many effects (including all
// original ProtoTracer effects) without future API changes.

// ── [SHADER:FUTURE] Shader Classes ──────────────────────────────────────────
// These types define the GPU-side post-processing shader system.
// Host-side encoding is stubbed; see GPUDriverController.h [SHADER:FUTURE] block.

enum PglShaderClass : uint8_t {
    PGL_SHADER_NONE         = 0x00,
    PGL_SHADER_CONVOLUTION  = 0x01,  // blur / smooth / sharpen via configurable kernel
    PGL_SHADER_DISPLACEMENT = 0x02,  // coordinate-space warp (chromatic aberration, etc.)
    PGL_SHADER_COLOR_ADJUST = 0x03,  // per-pixel color transform (feather, gamma, etc.)
};

// ── Supporting Enums ────────────────────────────────────────────────────────

/// Kernel shape for CONVOLUTION shaders.
enum PglKernelShape : uint8_t {
    PGL_KERNEL_BOX      = 0x00,  // equal-weight neighbours
    PGL_KERNEL_GAUSSIAN = 0x01,  // exp(-d²/2σ²) weighting
    PGL_KERNEL_TRIANGLE = 0x02,  // linearly decreasing weights
};

/// Waveform for DISPLACEMENT oscillators.
enum PglWaveform : uint8_t {
    PGL_WAVE_SAWTOOTH = 0x00,
    PGL_WAVE_SINE     = 0x01,
    PGL_WAVE_TRIANGLE = 0x02,
    PGL_WAVE_SQUARE   = 0x03,
};

/// Axis for DISPLACEMENT shaders.
enum PglDisplacementAxis : uint8_t {
    PGL_AXIS_X      = 0x00,  // horizontal
    PGL_AXIS_Y      = 0x01,  // vertical
    PGL_AXIS_RADIAL = 0x02,  // from center
};

/// Operation for COLOR_ADJUST shaders.
enum PglColorAdjustOp : uint8_t {
    PGL_COLOR_EDGE_FEATHER  = 0x00,  // dim pixels adjacent to black
    PGL_COLOR_THRESHOLD     = 0x01,  // binary threshold at strength
    PGL_COLOR_GAMMA         = 0x02,  // pow(c, param2)
    PGL_COLOR_INVERT        = 0x03,  // 1-c
    PGL_COLOR_BRIGHTNESS    = 0x04,  // c + strength
    PGL_COLOR_CONTRAST      = 0x05,  // (c - 0.5) * strength + 0.5
    PGL_COLOR_EDGE_DETECT   = 0x06,  // Sobel edge detection
};

static constexpr uint8_t PGL_MAX_SHADERS_PER_CAMERA = 4;

// ── Per-Class Parameter Structs ─────────────────────────────────────────────
//
// Each struct is stored in PglCmdSetShader::params[20].
// Structs are <= 20 bytes; unused trailing bytes are zero.

/// CONVOLUTION parameters.
///   angle=0°→ horizontal, angle=90°→ vertical, anglePeriod>0→ auto-rotate
///   separable=1 → 2D kernel (4-neighbour), else 1D directional kernel
struct PglShaderParamsConvolution {
    uint8_t kernelShape;    // PglKernelShape
    uint8_t radius;         // kernel half-width in pixels (1-32)
    uint8_t separable;      // 0 = 1D directional, 1 = 2D separable (4-neighbour)
    uint8_t _pad;
    float   angle;          // direction angle in degrees (0 = horizontal, 90 = vertical)
    float   anglePeriod;    // if > 0, angle auto-rotates with this period (seconds)
    float   sigma;          // gaussian σ (only when kernelShape = GAUSSIAN), or
                            // smoothing weight for separable mode (0.0–1.0)
};
static_assert(sizeof(PglShaderParamsConvolution) == 16, "PglShaderParamsConvolution must be 16 bytes");

/// DISPLACEMENT parameters.
///   Shifts sample coordinates per a wave function.  When perChannel=1,
///   R/G/B channels are displaced separately → chromatic aberration.
struct PglShaderParamsDisplacement {
    uint8_t axis;           // PglDisplacementAxis
    uint8_t perChannel;     // 0 = uniform, 1 = chromatic split (R/G/B 120° apart)
    uint8_t amplitude;      // max displacement in pixels (1-32)
    uint8_t waveform;       // PglWaveform
    float   period;         // primary oscillator period (seconds), 0 = static
    float   frequency;      // spatial frequency multiplier (default 1.0)
    float   phase1Period;   // secondary oscillator (radial mode)
    float   phase2Period;   // tertiary oscillator (radial mode)
};
static_assert(sizeof(PglShaderParamsDisplacement) == 20, "PglShaderParamsDisplacement must be 20 bytes");

/// COLOR_ADJUST parameters.
///   Modifies per-pixel colour.  `operation` selects the transform;
///   `strength` and `param2` are operation-specific.
struct PglShaderParamsColorAdjust {
    uint8_t operation;      // PglColorAdjustOp
    uint8_t _pad[3];
    float   strength;       // primary control (meaning varies per operation)
    float   param2;         // secondary control (gamma exponent, etc.)
};
static_assert(sizeof(PglShaderParamsColorAdjust) == 12, "PglShaderParamsColorAdjust must be 12 bytes");

// ── Wire Command ────────────────────────────────────────────────────────────

// CMD_SET_SHADER (0x83) — general screen-space shader slot assignment
struct PglCmdSetShader {
    uint8_t cameraId;       // which camera this shader applies to
    uint8_t shaderSlot;     // 0..PGL_MAX_SHADERS_PER_CAMERA-1, applied in order
    uint8_t shaderClass;    // PglShaderClass enum
    float   intensity;      // global mix factor (0.0 = bypass, 1.0 = full)
    uint8_t params[20];     // class-specific parameters (see PglShaderParams*)
};

// CMD_CREATE_MESH (0x01) — header only; variable-length arrays follow
struct PglCmdCreateMeshHeader {
    uint16_t meshId;
    uint16_t vertexCount;
    uint16_t triangleCount;
    uint8_t  flags;         // bit0: hasUV
};
static_assert(sizeof(PglCmdCreateMeshHeader) == 7, "PglCmdCreateMeshHeader must be 7 bytes");

enum PglMeshFlags : uint8_t {
    PGL_MESH_HAS_UV = 0x01,
};

// CMD_DESTROY_MESH (0x02)
struct PglCmdDestroyMesh {
    uint16_t meshId;
};

// CMD_UPDATE_VERTICES (0x03) — header only; vertex array follows
struct PglCmdUpdateVerticesHeader {
    uint16_t meshId;
    uint16_t vertexCount;
};

// CMD_UPDATE_VERTICES_DELTA (0x04) — header only; delta array follows
struct PglCmdUpdateVerticesDeltaHeader {
    uint16_t meshId;
    uint16_t deltaCount;
};

struct PglVertexDelta {
    uint16_t index;
    float    x;
    float    y;
    float    z;
};
static_assert(sizeof(PglVertexDelta) == 14, "PglVertexDelta must be 14 bytes");

// CMD_CREATE_MATERIAL (0x10) — header only; type-specific params follow
struct PglCmdCreateMaterialHeader {
    uint16_t materialId;
    uint8_t  materialType;
    uint8_t  blendMode;
};

// CMD_UPDATE_MATERIAL (0x11) — header only; type-specific params follow
struct PglCmdUpdateMaterialHeader {
    uint16_t materialId;
};

// CMD_DESTROY_MATERIAL (0x12)
struct PglCmdDestroyMaterial {
    uint16_t materialId;
};

// CMD_CREATE_TEXTURE (0x18) — header only; pixel data follows
struct PglCmdCreateTextureHeader {
    uint16_t textureId;
    uint16_t width;
    uint16_t height;
    uint8_t  format;        // 0=RGB565, 1=RGB888
};

// CMD_DESTROY_TEXTURE (0x19)
struct PglCmdDestroyTexture {
    uint16_t textureId;
};

// CMD_SET_PIXEL_LAYOUT (0x20) — header; coordinate data follows
struct PglCmdSetPixelLayoutHeader {
    uint8_t  layoutId;
    uint16_t pixelCount;
    uint8_t  flags;         // bit0: isRectangular, bit1: reversed
};

enum PglLayoutFlags : uint8_t {
    PGL_LAYOUT_RECTANGULAR = 0x01,
    PGL_LAYOUT_REVERSED    = 0x02,
};

// Rectangular layout additional data (follows PglCmdSetPixelLayoutHeader)
struct PglRectLayoutData {
    PglVec2  size;
    PglVec2  position;
    uint16_t rowCount;
    uint16_t colCount;
};
static_assert(sizeof(PglRectLayoutData) == 20, "PglRectLayoutData must be 20 bytes");

#pragma pack(pop)

// ─── Material Types ─────────────────────────────────────────────────────────

enum PglMaterialType : uint8_t {
    PGL_MAT_SIMPLE         = 0x00,
    PGL_MAT_NORMAL         = 0x01,
    PGL_MAT_DEPTH          = 0x02,

    PGL_MAT_GRADIENT       = 0x10,

    PGL_MAT_LIGHT          = 0x20,

    PGL_MAT_SIMPLEX_NOISE  = 0x30,
    PGL_MAT_RAINBOW_NOISE  = 0x31,

    PGL_MAT_IMAGE          = 0x40,

    PGL_MAT_COMBINE        = 0x50,
    PGL_MAT_MASK           = 0x51,
    PGL_MAT_ANIMATOR       = 0x52,

    PGL_MAT_PRERENDERED    = 0xF0,
};

// ─── Blend Modes (matches Material::Method enum) ────────────────────────────

enum PglBlendMode : uint8_t {
    PGL_BLEND_BASE          = 0,
    PGL_BLEND_ADD           = 1,
    PGL_BLEND_SUBTRACT      = 2,
    PGL_BLEND_MULTIPLY      = 3,
    PGL_BLEND_DIVIDE        = 4,
    PGL_BLEND_DARKEN        = 5,
    PGL_BLEND_LIGHTEN       = 6,
    PGL_BLEND_SCREEN        = 7,
    PGL_BLEND_OVERLAY       = 8,
    PGL_BLEND_SOFTLIGHT     = 9,
    PGL_BLEND_REPLACE       = 10,
    PGL_BLEND_EFFICIENT_MASK = 11,
};

// ─── Texture Formats ────────────────────────────────────────────────────────

enum PglTextureFormat : uint8_t {
    PGL_TEX_RGB565 = 0,
    PGL_TEX_RGB888 = 1,
};

// ─── Material Parameter Structs (packed for wire) ───────────────────────────

#pragma pack(push, 1)

struct PglParamSimple {
    uint8_t r, g, b;
};

struct PglParamDepth {
    uint8_t nearR, nearG, nearB;
    uint8_t farR, farG, farB;
    float   nearZ;
    float   farZ;
};
static_assert(sizeof(PglParamDepth) == 14, "PglParamDepth must be 14 bytes");

struct PglGradientStop {
    float   position;
    uint8_t r, g, b;
};

struct PglParamGradientHeader {
    uint8_t stopCount;
    // followed by stopCount * PglGradientStop
    // followed by:
    // uint8_t axis;      // 0=X, 1=Y, 2=Z
    // float   rangeMin;
    // float   rangeMax;
};

struct PglParamLight {
    float   lightDirX, lightDirY, lightDirZ;
    uint8_t ambientR, ambientG, ambientB;
    uint8_t diffuseR, diffuseG, diffuseB;
};
static_assert(sizeof(PglParamLight) == 18, "PglParamLight must be 18 bytes");

struct PglParamSimplexNoise {
    float   scaleX, scaleY, scaleZ, speed;
    uint8_t colorAR, colorAG, colorAB;
    uint8_t colorBR, colorBG, colorBB;
};
static_assert(sizeof(PglParamSimplexNoise) == 22, "PglParamSimplexNoise must be 22 bytes");

struct PglParamRainbowNoise {
    float scale;
    float speed;
};

struct PglParamImage {
    uint16_t textureId;
    float    offsetX, offsetY;
    float    scaleX, scaleY;
};
static_assert(sizeof(PglParamImage) == 18, "PglParamImage must be 18 bytes");

struct PglParamCombine {
    uint16_t materialIdA;
    uint16_t materialIdB;
    uint8_t  blendMode;
    float    opacity;
};
static_assert(sizeof(PglParamCombine) == 9, "PglParamCombine must be 9 bytes");

struct PglParamMask {
    uint16_t baseMaterialId;
    uint16_t maskMaterialId;
    float    threshold;
};
static_assert(sizeof(PglParamMask) == 8, "PglParamMask must be 8 bytes");

struct PglParamAnimator {
    uint16_t materialIdA;
    uint16_t materialIdB;
    uint8_t  interpMode;
    float    ratio;
};
static_assert(sizeof(PglParamAnimator) == 9, "PglParamAnimator must be 9 bytes");

struct PglParamPreRendered {
    uint16_t textureId;
};

#pragma pack(pop)

// ─── GPU Architecture Identification ────────────────────────────────────────

enum PglGpuArch : uint8_t {
    PGL_ARCH_UNKNOWN       = 0x00,
    PGL_ARCH_ARM_CM33      = 0x01,  // ARM Cortex-M33 (e.g., RP2350 ARM mode)
    PGL_ARCH_RISCV_HAZARD3 = 0x02,  // Hazard3 RISC-V  (e.g., RP2350 RISC-V mode)
    PGL_ARCH_RISCV_CUSTOM  = 0x03,  // Custom RISC-V core (user GPU)
    PGL_ARCH_ARM_CM4       = 0x04,  // ARM Cortex-M4
    PGL_ARCH_ARM_CM7       = 0x05,  // ARM Cortex-M7
    PGL_ARCH_FPGA          = 0x10,  // FPGA soft-core or hardened pipeline
    PGL_ARCH_RISCV_RV32I   = 0x20,  // Generic RV32I
    PGL_ARCH_RISCV_RV32IMF = 0x21,  // RV32I + M (multiply) + F (float)
};

// ─── I2C Configuration Registers ────────────────────────────────────────────

static constexpr uint8_t PGL_I2C_DEFAULT_ADDR = 0x3C;

enum PglI2CRegister : uint8_t {
    PGL_REG_SET_BRIGHTNESS   = 0x01,
    PGL_REG_SET_PANEL_CONFIG = 0x02,
    PGL_REG_SET_SCAN_RATE    = 0x03,
    PGL_REG_CLEAR_DISPLAY    = 0x04,
    PGL_REG_SET_COLOR_ORDER  = 0x05,
    PGL_REG_SET_GAMMA_TABLE  = 0x06,
    PGL_REG_SET_CLOCK_SPEED  = 0x07,
    PGL_REG_CAPABILITY_QUERY = 0x09,  // Returns PglCapabilityResponse
    PGL_REG_STATUS_REQUEST   = 0x0A,
    PGL_REG_RESET_GPU        = 0x0B,
};

#pragma pack(push, 1)

struct PglStatusResponse {
    uint16_t currentFPS;
    uint16_t droppedFrames;
    uint16_t freeMemory16;  // free bytes / 16
    int8_t   temperature;
    uint8_t  flags;         // bit0: renderBusy, bit1: bufferOverflow
};
static_assert(sizeof(PglStatusResponse) == 8, "PglStatusResponse must be 8 bytes");

enum PglStatusFlags : uint8_t {
    PGL_STATUS_RENDER_BUSY    = 0x01,
    PGL_STATUS_BUFFER_OVERFLOW = 0x02,
};

/// Returned by PGL_REG_CAPABILITY_QUERY (0x09). Allows the host to discover
/// what GPU core is on the other end of the wire.
struct PglCapabilityResponse {
    uint8_t  protoVersion;   // ProtoGL protocol version (currently 3 = v0.3)
    uint8_t  gpuArch;        // PglGpuArch enum value
    uint8_t  coreCount;      // Number of render cores (e.g., 2 for RP2350)
    uint8_t  coreFreqMHz;    // Core clock in MHz (e.g., 150)
    uint16_t sramKB;         // Total SRAM in KB (e.g., 520 for RP2350)
    uint16_t maxVertices;    // Max vertices the GPU can hold
    uint16_t maxTriangles;   // Max triangles the GPU can hold
    uint16_t maxMeshes;      // Max mesh slots
    uint16_t maxMaterials;   // Max material slots
    uint8_t  maxTextures;    // Max texture slots
    uint8_t  flags;          // bit0: hasHWFloat, bit1: hasUnalignedAccess,
                             // bit2: hasDSP, bit3: hasSIMD
};
static_assert(sizeof(PglCapabilityResponse) == 16, "PglCapabilityResponse must be 16 bytes");

enum PglCapabilityFlags : uint8_t {
    PGL_CAP_HW_FLOAT         = 0x01,  // Hardware FPU present
    PGL_CAP_UNALIGNED_ACCESS = 0x02,  // Safe unaligned loads (ARM CM33)
    PGL_CAP_DSP              = 0x04,  // DSP extension (ARM DSP, RISC-V P)
    PGL_CAP_SIMD             = 0x08,  // SIMD extension (Helium, RISC-V V)
};

#pragma pack(pop)
