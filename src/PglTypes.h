/**
 * @file PglTypes.h
 * @brief ProtoGL wire-format types and structures.
 *
 * These structs are designed for direct serialization (little-endian, packed).
 * They are shared between the ESP32-S3 host encoder and the RP2350 GPU decoder.
 *
 * ProtoGL API Specification v0.7.3 — M12 implementation audit alignment
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
using PglDisplay  = uint8_t;   ///< Display driver slot handle (0–3, M11)
using PglPool     = uint16_t;  ///< Memory pool handle (M11)
using PglLayer    = uint8_t;   ///< 2D compositing layer handle (0–7, M12)

static constexpr PglMesh     PGL_INVALID_MESH     = 0xFFFF;
static constexpr PglMaterial PGL_INVALID_MATERIAL  = 0xFFFF;
static constexpr PglTexture  PGL_INVALID_TEXTURE   = 0xFFFF;
static constexpr PglCamera   PGL_INVALID_CAMERA    = 0xFF;
static constexpr PglLayout   PGL_INVALID_LAYOUT    = 0xFF;
static constexpr PglDisplay  PGL_INVALID_DISPLAY   = 0xFF;
static constexpr PglPool     PGL_INVALID_POOL      = 0xFFFF;
static constexpr PglLayer    PGL_INVALID_LAYER     = 0xFF;

// ─── GPU Memory Types ───────────────────────────────────────────────────────

/// Memory tier identifier — selects which physical memory to target.
enum PglMemTier : uint8_t {
    PGL_TIER_SRAM       = 0,   // Tier 0: On-chip SRAM (520 KB, 1-cycle)
    PGL_TIER_QSPI_A     = 1,   // Tier 1: QSPI Channel A VRAM (PIO2 SM0+SM1, RP2350B)
    PGL_TIER_QSPI_B     = 2,   // Tier 2: QSPI Channel B VRAM (PIO2 SM2+SM3, RP2350B)
    PGL_TIER_AUTO       = 0xFF, // Let GPU decide (tiering manager)

    // Legacy aliases (wire-compatible — enum values unchanged)
    PGL_TIER_OPI_PSRAM  = PGL_TIER_QSPI_A,
    PGL_TIER_QSPI_PSRAM = PGL_TIER_QSPI_B,
};

/// QSPI VRAM channel configuration (reported in extended status).
/// Replaces the old PglPio2MemMode (which assumed OPI/single-QSPI).
enum PglQspiVramMode : uint8_t {
    PGL_QSPI_VRAM_NONE           = 0,   // No external VRAM (RP2350A, or RP2350B unpopulated)
    PGL_QSPI_VRAM_SINGLE_CHANNEL = 1,   // QSPI Channel A only (1–2 chips)
    PGL_QSPI_VRAM_DUAL_CHANNEL   = 2,   // Both Channel A + B (up to 2+2 chips)
};

/// Legacy alias
using PglPio2MemMode = PglQspiVramMode;
static constexpr PglQspiVramMode PGL_PIO2_MODE_NONE = PGL_QSPI_VRAM_NONE;

/// Chip type detected on a QSPI VRAM chip-select (reported in extended status).
/// Determines memory tier placement policy: MRAM has no random-access penalty,
/// enabling more aggressive demotion of LUTs/materials/textures from SRAM.
enum PglQspiChipType : uint8_t {
    PGL_QSPI_CHIP_NONE           = 0,     // Chip-select unpopulated
    PGL_QSPI_CHIP_MRAM_MR10Q010  = 1,     // 128 KB MRAM, no random-access penalty
    PGL_QSPI_CHIP_PSRAM_APS6408L = 2,     // 8 MB PSRAM, row-buffer miss penalty
    PGL_QSPI_CHIP_PSRAM_ESP      = 3,     // 8 MB PSRAM, row-buffer miss penalty
    PGL_QSPI_CHIP_UNKNOWN        = 0xFE,  // Responded but unrecognized
};

/// Opaque handle for GPU-side memory allocations.
using PglMemHandle = uint16_t;
static constexpr PglMemHandle PGL_INVALID_MEM_HANDLE = 0xFFFF;

/// Maximum number of concurrent GPU memory allocations tracked.
static constexpr uint16_t PGL_MAX_MEM_ALLOCATIONS = 256;

/// Resource class identifiers for tier placement hints.
enum PglMemResourceClass : uint8_t {
    PGL_RES_CLASS_MESH     = 0,
    PGL_RES_CLASS_MATERIAL = 1,
    PGL_RES_CLASS_TEXTURE  = 2,
    PGL_RES_CLASS_LAYOUT   = 3,
    PGL_RES_CLASS_GENERIC  = 4,  // Raw user allocation
};

// ─── Limits ─────────────────────────────────────────────────────────────────

static constexpr uint16_t PGL_MAX_MESHES     = 256;
static constexpr uint16_t PGL_MAX_MATERIALS   = 256;
static constexpr uint16_t PGL_MAX_TEXTURES    = 64;
static constexpr uint8_t  PGL_MAX_CAMERAS     = 4;
static constexpr uint8_t  PGL_MAX_LAYOUTS     = 8;
static constexpr uint8_t  PGL_MAX_DRAW_CALLS  = 64;

static constexpr uint16_t PGL_MAX_VERTICES    = 2048;
static constexpr uint16_t PGL_MAX_TRIANGLES   = 1024;

// ─── Display Limits (M11) ───────────────────────────────────────────────────

static constexpr uint8_t  PGL_MAX_DISPLAYS    = 4;    ///< Max simultaneously active display drivers
static constexpr uint8_t  PGL_MAX_MEM_POOLS   = 16;   ///< Max concurrent memory pools

// ─── 2D Layer Limits (M12) ──────────────────────────────────────────────────

static constexpr uint8_t  PGL_MAX_LAYERS      = 8;    ///< Max compositing layers (0 = 3D, 1–7 = 2D)
static constexpr uint8_t  PGL_LAYER_3D        = 0;    ///< Layer 0 is always the 3D scene layer
static constexpr uint8_t  PGL_MAX_2D_DRAW_CMDS = 128; ///< Max queued 2D draw commands per frame

/// Display driver type identifiers.
/// The GPU firmware registers one driver per type; DisplayManager routes by ID.
enum PglDisplayType : uint8_t {
    PGL_DISPLAY_NONE         = 0x00,  ///< No display / slot empty
    PGL_DISPLAY_HUB75        = 0x01,  ///< HUB75 LED matrix (PIO0 + BCM, main display)
    PGL_DISPLAY_I2C_HUD      = 0x02,  ///< I2C1 SSD1306/SSD1309 128×64 mono OLED (HUD)
    PGL_DISPLAY_SPI_LCD      = 0x03,  ///< SPI LCD (future — reserved for M12)
    PGL_DISPLAY_DVI          = 0x04,  ///< DVI-D (PIO TMDS, future — reserved for M12)
    PGL_DISPLAY_QSPI_LCD     = 0x05,  ///< QSPI LCD (future — reserved for M12)
};

/// Display configuration flags (bitmask).
enum PglDisplayConfigFlags : uint8_t {
    PGL_DISP_FLAG_ENABLED    = 0x01,  ///< Enable output on this display
    PGL_DISP_FLAG_MIRROR     = 0x02,  ///< Mirror the primary display's framebuffer
    PGL_DISP_FLAG_FLIP_H     = 0x04,  ///< Horizontal flip
    PGL_DISP_FLAG_FLIP_V     = 0x08,  ///< Vertical flip
    PGL_DISP_FLAG_HUD_AUTO   = 0x10,  ///< Auto-render GPU status overlay (I2C HUD only)
};

/// Display pixel format.
enum PglDisplayPixelFormat : uint8_t {
    PGL_PIXFMT_RGB565        = 0x00,  ///< 16-bit RGB (HUB75, SPI LCD, DVI)
    PGL_PIXFMT_MONO1         = 0x01,  ///< 1-bit monochrome (I2C HUD OLED)
    PGL_PIXFMT_RGB888        = 0x02,  ///< 24-bit RGB (DVI-D native)
};

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
static_assert(sizeof(PglTransform) == 96, "PglTransform must be 96 bytes");

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
static_assert(sizeof(PglCmdDrawObject) == 101, "PglCmdDrawObject must be 101 bytes");

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
    PGL_SHADER_PROGRAM      = 0x04,  // programmable bytecode shader (PSB VM)
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

// ── Programmable Shader Commands (v0.6) ─────────────────────────────────────

// CMD_CREATE_SHADER_PROGRAM (0x84) — header; followed by PSB bytecode blob
struct PglCmdCreateShaderProgramHeader {
    uint16_t programId;     // GPU-side handle (0–PGL_MAX_SHADER_PROGRAMS-1)
    uint16_t bytecodeSize;  // Total size of PSB blob following this header
};

// CMD_DESTROY_SHADER_PROGRAM (0x85)
struct PglCmdDestroyShaderProgram {
    uint16_t programId;
};

// CMD_BIND_SHADER_PROGRAM (0x86)
// Assigns a compiled program to a camera's shader slot.
// The existing CMD_SET_SHADER (0x83) continues to work for built-in classes.
struct PglCmdBindShaderProgram {
    uint8_t  cameraId;
    uint8_t  shaderSlot;
    uint16_t programId;      // 0xFFFF = unbind (clear slot)
    float    intensity;      // global mix factor (0.0 = bypass, 1.0 = full)
};

// CMD_SET_SHADER_UNIFORM (0x87) — header; followed by componentCount × float
struct PglCmdSetShaderUniformHeader {
    uint16_t programId;
    uint8_t  uniformSlot;    // 0–15
    uint8_t  componentCount; // 1=float, 2=vec2, 3=vec3, 4=vec4
    // Followed by: componentCount × float (4–16 bytes)
};

// Capability flag for shader VM support
// NOTE: This is defined as bit 8 (0x100), which exceeds the capacity of the
// current `PglCapabilityResponse.flags` uint8_t field.  All 8 bits of that
// field are already allocated (0x01–0x80).  A future protocol version should
// widen `flags` to uint16_t (requiring a struct layout change and version bump)
// to accommodate this and additional capability bits.
static constexpr uint32_t PGL_CAP_SHADER_VM = (1u << 8);

// Maximum number of loaded shader programs on the GPU
static constexpr uint8_t PGL_MAX_SHADER_PROGRAMS = 16;

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

// ─── GPU Memory Access Command Payloads (0x30 – 0x3F) ──────────────────────

/// CMD_MEM_WRITE (0x30) — header only; raw data bytes follow.
/// Writes `size` bytes to address `address` in the specified memory tier.
/// Payload on wire: PglCmdMemWriteHeader + size bytes of data.
struct PglCmdMemWriteHeader {
    uint8_t  tier;          // PglMemTier
    uint32_t address;       // Byte offset within tier address space
    uint32_t size;          // Number of data bytes that follow
};
static_assert(sizeof(PglCmdMemWriteHeader) == 9, "PglCmdMemWriteHeader must be 9 bytes");

/// CMD_MEM_READ_REQUEST (0x31) — request GPU to stage memory for I2C readback.
/// After receiving this, GPU copies the requested range into an internal
/// staging buffer accessible via PGL_REG_MEM_READ_DATA (I2C).
struct PglCmdMemReadRequest {
    uint8_t  tier;          // PglMemTier
    uint32_t address;       // Byte offset within tier
    uint16_t size;          // Bytes to stage (max 4096 per request)
};
static_assert(sizeof(PglCmdMemReadRequest) == 7, "PglCmdMemReadRequest must be 7 bytes");

/// Maximum bytes that can be staged for a single read-request.
static constexpr uint16_t PGL_MEM_READ_MAX_SIZE = 4096;

/// CMD_MEM_SET_RESOURCE_TIER (0x32) — tier placement hint for a resource.
/// Tells the GPU's tiering manager which tier a resource should prefer.
struct PglCmdSetResourceTier {
    uint8_t  resourceClass; // PglMemResourceClass
    uint16_t resourceId;    // Handle (meshId, materialId, textureId, etc.)
    uint8_t  preferredTier; // PglMemTier (or PGL_TIER_AUTO)
    uint8_t  flags;         // bit0: pinned (never demote/promote)
};
static_assert(sizeof(PglCmdSetResourceTier) == 5, "PglCmdSetResourceTier must be 5 bytes");

enum PglResourceTierFlags : uint8_t {
    PGL_TIER_FLAG_PINNED = 0x01,  // Prevent automatic tier migration
};

/// CMD_MEM_ALLOC (0x33) — allocate a region in a specific GPU memory tier.
/// Result (handle + address) can be read via PGL_REG_MEM_ALLOC_RESULT (I2C).
struct PglCmdMemAlloc {
    uint8_t  tier;          // PglMemTier (PGL_TIER_AUTO not allowed)
    uint32_t size;          // Bytes requested
    uint16_t tag;           // Caller-defined tag for identification/debug
};
static_assert(sizeof(PglCmdMemAlloc) == 7, "PglCmdMemAlloc must be 7 bytes");

/// CMD_MEM_FREE (0x34) — free a previously allocated GPU memory region.
struct PglCmdMemFree {
    PglMemHandle handle;    // Handle returned by MEM_ALLOC
};
static_assert(sizeof(PglCmdMemFree) == 2, "PglCmdMemFree must be 2 bytes");

/// CMD_FRAMEBUFFER_CAPTURE (0x35) — snapshot the framebuffer for readback.
/// GPU copies the selected buffer into the staging area. Host reads it
/// via PGL_REG_MEM_READ_DATA (I2C) in 32-byte chunks.
struct PglCmdFramebufferCapture {
    uint8_t bufferSelect;   // 0=front (display), 1=back (in-progress)
    uint8_t format;         // PglTextureFormat (0=RGB565, 1=RGB888)
};
static_assert(sizeof(PglCmdFramebufferCapture) == 2, "PglCmdFramebufferCapture must be 2 bytes");

/// CMD_MEM_COPY (0x36) — GPU-internal copy between memory regions/tiers.
/// Executes entirely on-GPU (no host data transfer needed).
struct PglCmdMemCopy {
    uint8_t  srcTier;       // Source PglMemTier
    uint32_t srcAddress;    // Source byte offset
    uint8_t  dstTier;       // Destination PglMemTier
    uint32_t dstAddress;    // Destination byte offset
    uint32_t size;          // Bytes to copy
};
static_assert(sizeof(PglCmdMemCopy) == 14, "PglCmdMemCopy must be 14 bytes");

// ─── Memory Pool Command Payloads (M11) ─────────────────────────────────────

/// CMD_MEM_POOL_CREATE (0x38) — create a fixed-size block pool.
/// Allocates a contiguous region from the tier's free-list, then subdivides
/// into `blockCount` blocks of `blockSize` bytes each.
struct PglCmdMemPoolCreate {
    uint8_t  tier;          ///< PglMemTier (PGL_TIER_AUTO not allowed)
    uint16_t blockSize;     ///< Size of each block in bytes (4–4096, must be power-of-2)
    uint16_t blockCount;    ///< Number of blocks to pre-allocate
    uint16_t tag;           ///< User-defined tag for debug/identification
};
static_assert(sizeof(PglCmdMemPoolCreate) == 7, "PglCmdMemPoolCreate must be 7 bytes");

/// CMD_MEM_POOL_ALLOC (0x39) — allocate one block from a pool.
/// The resulting block index is available via PGL_REG_MEM_ALLOC_RESULT I2C.
struct PglCmdMemPoolAlloc {
    PglPool poolHandle;     ///< Pool handle (from prior create — read via I2C)
};
static_assert(sizeof(PglCmdMemPoolAlloc) == 2, "PglCmdMemPoolAlloc must be 2 bytes");

/// CMD_MEM_POOL_FREE (0x3A) — free one block back to a pool.
struct PglCmdMemPoolFree {
    PglPool  poolHandle;    ///< Pool handle
    uint16_t blockIndex;    ///< Block index within the pool (0-based)
};
static_assert(sizeof(PglCmdMemPoolFree) == 4, "PglCmdMemPoolFree must be 4 bytes");

/// CMD_MEM_POOL_DESTROY (0x3B) — destroy pool and return memory to tier.
struct PglCmdMemPoolDestroy {
    PglPool poolHandle;     ///< Pool handle to destroy
};
static_assert(sizeof(PglCmdMemPoolDestroy) == 2, "PglCmdMemPoolDestroy must be 2 bytes");

// ─── Display Command Payloads (M11) ─────────────────────────────────────────

/// CMD_DISPLAY_CONFIGURE (0x90) — configure a display driver slot.
/// Sent once at startup or when switching display modes.
struct PglCmdDisplayConfigure {
    PglDisplay displayId;   ///< Display slot (0–PGL_MAX_DISPLAYS-1)
    uint8_t    displayType; ///< PglDisplayType to activate in this slot
    uint16_t   width;       ///< Display width in pixels (0 = use driver default)
    uint16_t   height;      ///< Display height in pixels (0 = use driver default)
    uint8_t    pixelFormat; ///< PglDisplayPixelFormat
    uint8_t    flags;       ///< PglDisplayConfigFlags bitmask
    uint8_t    brightness;  ///< Initial brightness (0–255)
    uint8_t    reserved;    ///< Padding / future use
};
static_assert(sizeof(PglCmdDisplayConfigure) == 10, "PglCmdDisplayConfigure must be 10 bytes");

/// CMD_DISPLAY_SET_REGION (0x91) — set partial-update region for a display.
/// Useful for OLED/LCD displays that support windowed updates.
struct PglCmdDisplaySetRegion {
    PglDisplay displayId;   ///< Display slot
    uint16_t   x;           ///< Left edge of region
    uint16_t   y;           ///< Top edge of region
    uint16_t   w;           ///< Width of region
    uint16_t   h;           ///< Height of region
};
static_assert(sizeof(PglCmdDisplaySetRegion) == 9, "PglCmdDisplaySetRegion must be 9 bytes");

/// Display capabilities response — returned by PGL_REG_DISPLAY_CAPS I2C.
/// Reports what a display driver can do (queried per-slot).
struct PglDisplayCaps {
    uint8_t  displayType;   ///< PglDisplayType
    uint16_t width;         ///< Native resolution width
    uint16_t height;        ///< Native resolution height
    uint8_t  pixelFormat;   ///< Native PglDisplayPixelFormat
    uint8_t  maxBrightness; ///< Max brightness (255 = full range)
    uint8_t  flags;         ///< Supported PglDisplayConfigFlags
    uint16_t refreshHz;     ///< Native refresh rate in Hz
    uint16_t framebufKB;    ///< Framebuffer size in KB
    uint8_t  pioUsage;      ///< Number of PIO SMs consumed (0 for I2C drivers)
    uint8_t  dmaUsage;      ///< Number of DMA channels consumed
    uint8_t  reserved[2];   ///< Padding to 16 bytes
};
static_assert(sizeof(PglDisplayCaps) == 16, "PglDisplayCaps must be 16 bytes");

/// Memory pool status response — returned by PGL_REG_MEM_POOL_STATUS I2C.
/// Reports current state of a specific memory pool.
struct PglMemPoolStatusResponse {
    PglPool  poolHandle;    ///< Pool handle queried
    uint8_t  tier;          ///< PglMemTier where pool resides
    uint16_t blockSize;     ///< Block size in bytes
    uint16_t blockCount;    ///< Total blocks in pool
    uint16_t freeCount;     ///< Currently free blocks
    uint16_t tag;           ///< User-defined tag
    uint8_t  status;        ///< 0 = OK, 1 = exhausted, 0xFF = invalid handle
};
static_assert(sizeof(PglMemPoolStatusResponse) == 12, "PglMemPoolStatusResponse must be 12 bytes");

/// Memory pool status codes.
enum PglPoolStatus : uint8_t {
    PGL_POOL_OK              = 0x00,  ///< Pool operational, free blocks available
    PGL_POOL_EXHAUSTED       = 0x01,  ///< All blocks allocated (alloc will fail)
    PGL_POOL_INVALID_HANDLE  = 0xFF,  ///< Pool handle not found
};

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

    // ── GPU Memory Access Registers (0x0C – 0x0F) ──
    PGL_REG_MEM_TIER_INFO    = 0x0C,  // Read: PglMemTierInfoResponse (per-tier stats)
    PGL_REG_MEM_READ_ADDR    = 0x0D,  // Write: PglMemReadSetup (set read address)
    PGL_REG_MEM_READ_DATA    = 0x0E,  // Read: 32 bytes from staged address, auto-increments
    PGL_REG_MEM_ALLOC_RESULT = 0x0F,  // Read: PglMemAllocResult (last alloc status)

    // ── Extended Diagnostics & Control (0x10 – 0x13) ──
    PGL_REG_SET_CLOCK_FREQ   = 0x10,  // Write: PglClockRequest (target MHz + voltage)
    PGL_REG_EXTENDED_STATUS  = 0x11,  // Read: PglExtendedStatusResponse (32 bytes)

    // ── Display & Memory Pool Registers (M11: 0x15 – 0x18) ──
    PGL_REG_DISPLAY_MODE     = 0x15,  // Write: uint8_t displayId → select active display for queries
                                      // Read:  uint8_t current active display type (PglDisplayType)
    PGL_REG_DISPLAY_CAPS     = 0x16,  // Read: PglDisplayCaps (16 bytes) for selected display slot
    PGL_REG_MEM_POOL_STATUS  = 0x18,  // Write: uint16_t poolHandle → select pool
                                      // Read:  PglMemPoolStatusResponse (12 bytes)

    // ── Memory Defrag & Persistence Registers (M12: 0x19, 0x1C) ──
    PGL_REG_MEM_DEFRAG_STATUS  = 0x19,  // Read: PglMemDefragStatusResponse (8 bytes)
    PGL_REG_MEM_PERSIST_STATUS = 0x1C,  // Read: PglMemPersistStatusResponse (12 bytes)

    // ── Display Frontend Registers (M12: 0x1D – 0x1E) ──
    PGL_REG_DIRTY_STATS          = 0x1D,  // Read: PglDirtyStatsResponse (8 bytes)
    PGL_REG_DMA_FILL_THRESHOLD   = 0x1E,  // Write: uint16_t threshold in pixels (default 128)
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

/// Extended status response — 32 bytes of detailed GPU diagnostics.
/// Returned by PGL_REG_EXTENDED_STATUS (0x11).
struct PglExtendedStatusResponse {
    // Core performance (bytes 0–7)
    uint16_t currentFPS;        // Measured render FPS
    uint16_t droppedFrames;     // Cumulative CRC/overflow drops
    uint8_t  gpuUsagePercent;   // 0–100 — fraction of frame time spent rendering
    uint8_t  core0UsagePercent; // 0–100 — Core 0 render load
    uint8_t  core1UsagePercent; // 0–100 — Core 1 render load
    uint8_t  flags;             // PglStatusFlags

    // Thermal + power (bytes 8–11)
    int16_t  temperatureQ8;     // Die temperature in Q8.8 fixed-point (°C × 256)
    uint16_t currentClockMHz;   // Actual running clock frequency in MHz

    // Memory (bytes 12–19)
    uint16_t sramFreeKB;        // Free internal SRAM in KB
    uint16_t opiVramTotalKB;    // PIO2 external memory total (OPI PSRAM or QSPI MRAM, 0 if not present)
    uint16_t opiVramFreeKB;     // PIO2 external memory free
    uint16_t qspiVramTotalKB;   // QSPI VRAM total (MRAM or PSRAM, 0 if not present)

    // Frame timing (bytes 20–27)
    uint16_t frameTimeUs;       // Last frame time in microseconds
    uint16_t rasterTimeUs;      // Rasterization time (both cores)
    uint16_t transferTimeUs;    // SPI receive + command parse time
    uint16_t hub75RefreshHz;    // HUB75 display refresh rate

    // Counters (bytes 28–31)
    uint16_t qspiVramFreeKB;    // QSPI VRAM free
    uint8_t  vramTierFlags;     // bit0: OPI detected, bit1: QSPI detected,
                                // bit2: OPI initialised, bit3: QSPI initialised
    uint8_t  qspiChipType;      // PglQspiChipType — detected chip on CS1
};
static_assert(sizeof(PglExtendedStatusResponse) == 32, "PglExtendedStatusResponse must be 32 bytes");

/// GPU VRAM tier presence flags (PglExtendedStatusResponse::vramTierFlags)
enum PglVramTierFlags : uint8_t {
    PGL_VRAM_OPI_DETECTED    = 0x01,  // PIO2 external memory detected (OPI PSRAM or QSPI MRAM)
    PGL_VRAM_QSPI_DETECTED   = 0x02,  // QSPI chip responded to RDID (MRAM 0x4B or PSRAM 0x9F)
    PGL_VRAM_OPI_INITIALISED  = 0x04,  // PIO2 external memory driver fully initialised
    PGL_VRAM_QSPI_INITIALISED = 0x08,  // QSPI CS1 driver fully initialised (MRAM or PSRAM)
};

/// Clock change request — written to PGL_REG_SET_CLOCK_FREQ (0x10)
struct PglClockRequest {
    uint16_t targetMHz;         // Desired system clock in MHz (0 = query only)
    uint8_t  voltageLevel;      // VREG voltage enum (0 = auto-select)
    uint8_t  flags;             // bit0: reconfigurePIO after change
};
static_assert(sizeof(PglClockRequest) == 4, "PglClockRequest must be 4 bytes");

enum PglClockFlags : uint8_t {
    PGL_CLOCK_RECONFIGURE_PIO = 0x01, // Recalculate PIO clock dividers
    PGL_CLOCK_THERMAL_AUTO    = 0x02, // Enable automatic thermal throttling
};

/// Returned by PGL_REG_CAPABILITY_QUERY (0x09). Allows the host to discover
/// what GPU core is on the other end of the wire.
struct PglCapabilityResponse {
    uint8_t  protoVersion;   // ProtoGL protocol version (currently 7 = v0.7)
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
    PGL_CAP_OPI_VRAM         = 0x10,  // PIO2 external memory detected (OPI PSRAM or QSPI MRAM)
    PGL_CAP_QSPI_VRAM        = 0x20,  // QSPI memory detected on QMI CS1 (MRAM or PSRAM)
    PGL_CAP_DYNAMIC_CLOCK    = 0x40,  // Supports runtime clock adjustment
    PGL_CAP_TEMP_SENSOR      = 0x80,  // On-chip temperature sensor available
};

// ─── GPU Memory I2C Response Structs ────────────────────────────────────────

/// Returned by PGL_REG_MEM_TIER_INFO (0x0C).
/// Reports per-tier capacity, usage, and cache statistics.
struct PglMemTierInfoResponse {
    // Tier 0 — SRAM
    uint16_t sramTotalKB;       // Total SRAM available for GPU use
    uint16_t sramFreeKB;        // Free SRAM
    // Tier 1 — PIO2 external memory (OPI PSRAM or QSPI MRAM)
    uint16_t opiTotalKB;        // Total PIO2 memory (0 if not present)
    uint16_t opiFreeKB;         // Free PIO2 memory
    uint8_t  opiEnabled;        // 1 if PIO2 external memory driver initialized
    // Tier 2 — QSPI CS1 (auto-detected: MRAM or PSRAM)
    uint16_t qspiTotalKB;       // Total QSPI memory (0 if not present)
    uint16_t qspiFreeKB;        // Free QSPI memory
    uint8_t  qspiEnabled;       // 1 if QSPI CS1 driver initialized
    // Cache / tiering stats
    uint16_t cachedEntries;      // Number of entries in SRAM cache arena
    uint16_t totalManagedAllocs; // Total allocations tracked by tier manager
    uint8_t  cacheHitRate;       // Percentage 0-100 (rolling average)
    uint8_t  reserved;           // Padding to 20 bytes
};
static_assert(sizeof(PglMemTierInfoResponse) == 20, "PglMemTierInfoResponse must be 20 bytes");

/// Written to PGL_REG_MEM_READ_ADDR (0x0D) to configure the next I2C read.
/// After writing this, the host reads PGL_REG_MEM_READ_DATA to get data.
struct PglMemReadSetup {
    uint8_t  tier;              // PglMemTier
    uint32_t address;           // Byte offset within tier
    uint16_t length;            // Total bytes to read (max PGL_MEM_READ_MAX_SIZE)
};
static_assert(sizeof(PglMemReadSetup) == 7, "PglMemReadSetup must be 7 bytes");

/// Bytes returned per PGL_REG_MEM_READ_DATA (0x0E) I2C read transaction.
/// Host must issue ceil(length / 32) reads to get all staged data.
static constexpr uint8_t PGL_MEM_READ_CHUNK_SIZE = 32;

/// Returned by PGL_REG_MEM_ALLOC_RESULT (0x0F) after a CMD_MEM_ALLOC.
struct PglMemAllocResult {
    PglMemHandle handle;        // Allocated handle (PGL_INVALID_MEM_HANDLE on failure)
    uint32_t     address;       // Tier-relative byte address of allocation
    uint8_t      status;        // PglMemAllocStatus
};
static_assert(sizeof(PglMemAllocResult) == 7, "PglMemAllocResult must be 7 bytes");

enum PglMemAllocStatus : uint8_t {
    PGL_ALLOC_OK               = 0x00,
    PGL_ALLOC_OUT_OF_MEMORY    = 0x01,
    PGL_ALLOC_INVALID_TIER     = 0x02,
    PGL_ALLOC_TIER_DISABLED    = 0x03,
    PGL_ALLOC_HANDLE_EXHAUSTED = 0x04,
};

// ─── 2D Layer Blend Modes (M12) ────────────────────────────────────────────

/// Blend modes for layer compositing (distinct from material blend modes).
enum PglLayerBlendMode : uint8_t {
    PGL_LAYER_BLEND_ALPHA    = 0,   ///< Standard alpha-over compositing
    PGL_LAYER_BLEND_ADDITIVE = 1,   ///< Additive blending (glow effects)
    PGL_LAYER_BLEND_MULTIPLY = 2,   ///< Multiply blending (darkening)
};

// ─── 2D Layer Command Payloads (0xA0 – 0xAC, M12) ──────────────────────────

/// CMD_LAYER_CREATE (0xA0) — create a 2D compositing layer with framebuffer.
struct PglCmdLayerCreate {
    PglLayer layerId;       ///< Layer slot (1–7; 0 is reserved for 3D)
    uint16_t width;         ///< Layer framebuffer width in pixels
    uint16_t height;        ///< Layer framebuffer height in pixels
    uint8_t  pixelFormat;   ///< PglDisplayPixelFormat (usually RGB565)
    uint8_t  blendMode;     ///< PglLayerBlendMode
    uint8_t  opacity;       ///< Initial opacity (0–255)
};
static_assert(sizeof(PglCmdLayerCreate) == 8, "PglCmdLayerCreate must be 8 bytes");

/// CMD_LAYER_DESTROY (0xA1) — destroy layer and free its framebuffer.
struct PglCmdLayerDestroy {
    PglLayer layerId;       ///< Layer slot to destroy
};
static_assert(sizeof(PglCmdLayerDestroy) == 1, "PglCmdLayerDestroy must be 1 byte");

/// CMD_LAYER_SET_PROPS (0xA2) — update layer compositing properties.
struct PglCmdLayerSetProps {
    PglLayer layerId;       ///< Target layer
    uint8_t  opacity;       ///< 0–255
    uint8_t  blendMode;     ///< PglLayerBlendMode
    int16_t  offsetX;       ///< Compositing offset X (signed)
    int16_t  offsetY;       ///< Compositing offset Y (signed)
};
static_assert(sizeof(PglCmdLayerSetProps) == 7, "PglCmdLayerSetProps must be 7 bytes");

/// CMD_DRAW_RECT_2D (0xA3) — draw a filled or outlined rectangle.
struct PglCmdDrawRect2D {
    PglLayer layerId;       ///< Target layer
    int16_t  x;             ///< Top-left X
    int16_t  y;             ///< Top-left Y
    uint16_t w;             ///< Width
    uint16_t h;             ///< Height
    uint16_t color;         ///< RGB565 color
    uint8_t  filled;        ///< 1 = filled, 0 = outline only
};
static_assert(sizeof(PglCmdDrawRect2D) == 12, "PglCmdDrawRect2D must be 12 bytes");

/// CMD_DRAW_LINE_2D (0xA4) — draw a line using Bresenham's algorithm.
struct PglCmdDrawLine2D {
    PglLayer layerId;       ///< Target layer
    int16_t  x0;            ///< Start X
    int16_t  y0;            ///< Start Y
    int16_t  x1;            ///< End X
    int16_t  y1;            ///< End Y
    uint16_t color;         ///< RGB565 color
};
static_assert(sizeof(PglCmdDrawLine2D) == 11, "PglCmdDrawLine2D must be 11 bytes");

/// CMD_DRAW_CIRCLE_2D (0xA5) — draw a filled or outlined circle.
struct PglCmdDrawCircle2D {
    PglLayer layerId;       ///< Target layer
    int16_t  cx;            ///< Center X
    int16_t  cy;            ///< Center Y
    uint16_t radius;        ///< Radius in pixels
    uint16_t color;         ///< RGB565 color
    uint8_t  filled;        ///< 1 = filled, 0 = outline only
};
static_assert(sizeof(PglCmdDrawCircle2D) == 10, "PglCmdDrawCircle2D must be 10 bytes");

/// CMD_DRAW_SPRITE (0xA6) — blit a texture to a layer position.
struct PglCmdDrawSprite {
    PglLayer   layerId;     ///< Target layer
    int16_t    x;           ///< Destination X
    int16_t    y;           ///< Destination Y
    PglTexture textureId;   ///< Source texture handle
    uint8_t    flags;       ///< bit0: flipH, bit1: flipV, bit2: srcIsSequence
};
static_assert(sizeof(PglCmdDrawSprite) == 8, "PglCmdDrawSprite must be 8 bytes");

/// Sprite flags bitmask.
enum PglSpriteFlags : uint8_t {
    PGL_SPRITE_FLIP_H        = 0x01,  ///< Horizontal flip
    PGL_SPRITE_FLIP_V        = 0x02,  ///< Vertical flip
    PGL_SPRITE_SRC_SEQUENCE  = 0x04,  ///< textureId is an ImageSequence, not a Texture
};

/// CMD_LAYER_CLEAR (0xA9) — clear a layer to a solid color.
struct PglCmdLayerClear {
    PglLayer layerId;       ///< Target layer
    uint16_t color;         ///< RGB565 clear color
};
static_assert(sizeof(PglCmdLayerClear) == 3, "PglCmdLayerClear must be 3 bytes");

/// CMD_DRAW_ROUNDED_RECT (0xAA) — rectangle with rounded corners.
struct PglCmdDrawRoundedRect {
    PglLayer layerId;       ///< Target layer
    int16_t  x;             ///< Top-left X
    int16_t  y;             ///< Top-left Y
    uint16_t w;             ///< Width
    uint16_t h;             ///< Height
    uint16_t radius;        ///< Corner radius in pixels
    uint16_t color;         ///< RGB565 color
    uint8_t  filled;        ///< 1 = filled, 0 = outline only
};
static_assert(sizeof(PglCmdDrawRoundedRect) == 14, "PglCmdDrawRoundedRect must be 14 bytes");

/// CMD_DRAW_ARC (0xAB) — draw an arc segment.
struct PglCmdDrawArc {
    PglLayer layerId;       ///< Target layer
    int16_t  cx;            ///< Center X
    int16_t  cy;            ///< Center Y
    uint16_t radius;        ///< Radius in pixels
    int16_t  startAngleDeg; ///< Start angle in degrees (0 = right, CCW)
    int16_t  endAngleDeg;   ///< End angle in degrees
    uint16_t color;         ///< RGB565 color
};
static_assert(sizeof(PglCmdDrawArc) == 13, "PglCmdDrawArc must be 13 bytes");

/// CMD_DRAW_TRIANGLE_2D (0xAC) — draw a filled 2D triangle.
struct PglCmdDrawTriangle2D {
    PglLayer layerId;       ///< Target layer
    int16_t  x0, y0;       ///< Vertex 0
    int16_t  x1, y1;       ///< Vertex 1
    int16_t  x2, y2;       ///< Vertex 2
    uint16_t color;         ///< RGB565 color
};
static_assert(sizeof(PglCmdDrawTriangle2D) == 15, "PglCmdDrawTriangle2D must be 15 bytes");

// ─── Defragmentation Command Payload (0x3C, M12) ───────────────────────────

/// CMD_MEM_DEFRAG (0x3C) — trigger memory defragmentation.
struct PglCmdMemDefrag {
    uint8_t  tier;          ///< PglMemTier to defragment (PGL_TIER_AUTO = all tiers)
    uint8_t  mode;          ///< 0 = incremental (budget-limited), 1 = urgent (full compact)
    uint16_t maxMoveKB;     ///< Max kilobytes to relocate this frame (incremental mode)
};
static_assert(sizeof(PglCmdMemDefrag) == 4, "PglCmdMemDefrag must be 4 bytes");

/// Defrag mode enum.
enum PglDefragMode : uint8_t {
    PGL_DEFRAG_INCREMENTAL = 0,  ///< Move at most maxMoveKB per frame
    PGL_DEFRAG_URGENT      = 1,  ///< Block pipeline, compact fully in one pass
};

// ─── Direct Framebuffer Write Payload (0x45, M12) ──────────────────────────

/// CMD_WRITE_FRAMEBUFFER (0x45) — header; followed by w×h×2 bytes (RGB565).
/// Writes a rectangular region of pixel data to a layer's framebuffer.
struct PglCmdWriteFramebufferHeader {
    PglLayer layerId;       ///< Target layer (0xFF = primary back buffer)
    int16_t  x;             ///< Destination X
    int16_t  y;             ///< Destination Y
    uint16_t w;             ///< Width in pixels
    uint16_t h;             ///< Height in pixels
    // Followed by: w * h * sizeof(uint16_t) bytes of RGB565 pixel data
};
static_assert(sizeof(PglCmdWriteFramebufferHeader) == 9, "PglCmdWriteFramebufferHeader must be 9 bytes");

// ─── Resource Persistence Payloads (0x46 – 0x48, M12) ──────────────────────

/// CMD_PERSIST_RESOURCE (0x46) — queue a resource for flash writeback.
struct PglCmdPersistResource {
    uint8_t  resourceClass; ///< PglMemResourceClass
    uint16_t resourceId;    ///< Resource handle
    uint8_t  flags;         ///< bit0: overwriteExisting
};
static_assert(sizeof(PglCmdPersistResource) == 4, "PglCmdPersistResource must be 4 bytes");

enum PglPersistFlags : uint8_t {
    PGL_PERSIST_OVERWRITE = 0x01,  ///< Overwrite existing flash entry if present
};

/// CMD_RESTORE_RESOURCE (0x47) — load a resource from flash manifest to VRAM.
struct PglCmdRestoreResource {
    uint8_t  resourceClass; ///< PglMemResourceClass
    uint16_t resourceId;    ///< Resource handle
};
static_assert(sizeof(PglCmdRestoreResource) == 3, "PglCmdRestoreResource must be 3 bytes");

/// CMD_QUERY_PERSISTENCE (0x48) — query persistence status.
/// Result available via PGL_REG_MEM_PERSIST_STATUS I2C register.
struct PglCmdQueryPersistence {
    uint8_t  resourceClass; ///< PglMemResourceClass (0xFF = query manifest-level)
    uint16_t resourceId;    ///< Resource handle (ignored if class = 0xFF)
};
static_assert(sizeof(PglCmdQueryPersistence) == 3, "PglCmdQueryPersistence must be 3 bytes");

// ─── M12 I2C Response Structs ──────────────────────────────────────────────

/// Returned by PGL_REG_MEM_DEFRAG_STATUS (0x19).
struct PglMemDefragStatusResponse {
    uint8_t  state;             ///< 0 = idle, 1 = in-progress, 2 = completed
    uint8_t  tier;              ///< Tier being/last defragmented
    uint16_t movedKB;           ///< KB relocated in last/current pass
    uint16_t fragmentCount;     ///< Current number of free-space fragments
    uint16_t largestFreeKB;     ///< Largest contiguous free block in KB
};
static_assert(sizeof(PglMemDefragStatusResponse) == 8, "PglMemDefragStatusResponse must be 8 bytes");

/// Defrag state enum.
enum PglDefragState : uint8_t {
    PGL_DEFRAG_IDLE       = 0,
    PGL_DEFRAG_ACTIVE     = 1,
    PGL_DEFRAG_COMPLETED  = 2,
};

/// Returned by PGL_REG_MEM_PERSIST_STATUS (0x1C).
struct PglMemPersistStatusResponse {
    uint8_t  state;             ///< 0 = idle, 1 = writing, 2 = restoring, 3 = error
    uint8_t  queueDepth;        ///< Pending persist requests (0–4)
    uint16_t manifestEntries;   ///< Total entries in flash manifest
    uint16_t manifestCapacity;  ///< Max manifest entries (64)
    uint32_t flashUsedBytes;    ///< Flash bytes consumed by persisted data
    uint16_t lastResourceId;    ///< Last resource that completed persist/restore
};
static_assert(sizeof(PglMemPersistStatusResponse) == 12, "PglMemPersistStatusResponse must be 12 bytes");

/// Persist state enum.
enum PglPersistState : uint8_t {
    PGL_PERSIST_IDLE     = 0,
    PGL_PERSIST_WRITING  = 1,
    PGL_PERSIST_RESTORING = 2,
    PGL_PERSIST_ERROR    = 3,
};

/// Returned by PGL_REG_DIRTY_STATS (0x1D).
struct PglDirtyStatsResponse {
    uint32_t pushedBytes;       ///< Bytes pushed to display last frame
    uint16_t skippedRegions;    ///< Number of clean regions skipped
    uint16_t totalRegions;      ///< Total scanline regions tracked
};
static_assert(sizeof(PglDirtyStatsResponse) == 8, "PglDirtyStatsResponse must be 8 bytes");

#pragma pack(pop)
