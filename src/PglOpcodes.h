/**
 * @file PglOpcodes.h
 * @brief ProtoGL command opcode definitions.
 *
 * Opcodes 0x01-0x2F: Resource management (create/update/destroy).
 * Opcodes 0x30-0x41: GPU memory access commands (pools, defrag, streaming, binding).
 * Opcodes 0x45-0x48: Direct framebuffer write + resource persistence.
 * Opcodes 0x80-0x8F: Per-frame rendering commands.
 * Opcodes 0x90-0x9F: Display driver configuration commands.
 * Opcodes 0xA0-0xB2: 2D drawing + layer compositing commands.
 *
 * ProtoGL API Specification v0.7.3 — M12 implementation audit alignment
 */

#pragma once

#include <cstdint>

// ─── Resource Commands (0x01 – 0x2F) ────────────────────────────────────────

static constexpr uint8_t PGL_CMD_CREATE_MESH              = 0x01;
static constexpr uint8_t PGL_CMD_DESTROY_MESH             = 0x02;
static constexpr uint8_t PGL_CMD_UPDATE_VERTICES          = 0x03;
static constexpr uint8_t PGL_CMD_UPDATE_VERTICES_DELTA    = 0x04;

static constexpr uint8_t PGL_CMD_CREATE_MATERIAL          = 0x10;
static constexpr uint8_t PGL_CMD_UPDATE_MATERIAL          = 0x11;
static constexpr uint8_t PGL_CMD_DESTROY_MATERIAL         = 0x12;

static constexpr uint8_t PGL_CMD_CREATE_TEXTURE           = 0x18;
static constexpr uint8_t PGL_CMD_DESTROY_TEXTURE          = 0x19;

static constexpr uint8_t PGL_CMD_SET_PIXEL_LAYOUT         = 0x20;

// ─── GPU Memory Access Commands (0x30 – 0x3F) ──────────────────────────────
// These commands allow the host to directly read/write GPU device memory
// across all tiers (SRAM, OPI PSRAM, QSPI MRAM), allocate/free regions,
// control resource tier placement, and capture the framebuffer.

static constexpr uint8_t PGL_CMD_MEM_WRITE               = 0x30;  // Write bytes → tier:addr
static constexpr uint8_t PGL_CMD_MEM_READ_REQUEST        = 0x31;  // Stage data for I2C readback
static constexpr uint8_t PGL_CMD_MEM_SET_RESOURCE_TIER   = 0x32;  // Tier placement hint
static constexpr uint8_t PGL_CMD_MEM_ALLOC               = 0x33;  // Allocate region in tier
static constexpr uint8_t PGL_CMD_MEM_FREE                = 0x34;  // Free allocated region
static constexpr uint8_t PGL_CMD_FRAMEBUFFER_CAPTURE     = 0x35;  // Snapshot FB for readback
static constexpr uint8_t PGL_CMD_MEM_COPY                = 0x36;  // GPU-internal tier-to-tier copy

// ─── Memory Pool Commands (0x38 – 0x3B) ─────────────────────────────────────
// Fixed-size block pool allocator — O(1) alloc/free, zero fragmentation.
// Pools are backed by contiguous allocations from the tier's free-list.

static constexpr uint8_t PGL_CMD_MEM_POOL_CREATE         = 0x38;  // Create fixed-size block pool
static constexpr uint8_t PGL_CMD_MEM_POOL_ALLOC          = 0x39;  // Allocate one block from pool
static constexpr uint8_t PGL_CMD_MEM_POOL_FREE           = 0x3A;  // Free one block back to pool
static constexpr uint8_t PGL_CMD_MEM_POOL_DESTROY        = 0x3B;  // Destroy pool + return memory

// ─── Defragmentation Command (0x3C) — M12 ───────────────────────────────────

static constexpr uint8_t PGL_CMD_MEM_DEFRAG              = 0x3C;  // Trigger incremental/urgent defrag

// ─── Streaming Commands (0x3D – 0x3F) — M13 ────────────────────────────────

static constexpr uint8_t PGL_CMD_STREAM_BEGIN            = 0x3D;  // Begin multi-frame upload
static constexpr uint8_t PGL_CMD_STREAM_DATA             = 0x3E;  // Stream data chunk
static constexpr uint8_t PGL_CMD_STREAM_COMMIT           = 0x3F;  // Commit streamed resource

// ─── Resource Binding Commands (0x40 – 0x41) — M13 ─────────────────────────

static constexpr uint8_t PGL_CMD_MEM_BIND_RESOURCE       = 0x40;  // Bind resource to indirection slot
static constexpr uint8_t PGL_CMD_MEM_UNBIND_RESOURCE     = 0x41;  // Unbind resource from slot

// ─── Direct Framebuffer Write + Persistence (0x45 – 0x48) — M12 ────────────

static constexpr uint8_t PGL_CMD_WRITE_FRAMEBUFFER       = 0x45;  // Write pixel rect to layer/FB
static constexpr uint8_t PGL_CMD_PERSIST_RESOURCE        = 0x46;  // Queue async flash writeback
static constexpr uint8_t PGL_CMD_RESTORE_RESOURCE        = 0x47;  // Restore resource from flash
static constexpr uint8_t PGL_CMD_QUERY_PERSISTENCE       = 0x48;  // Query persistence status

// ─── Per-Frame Rendering Commands (0x80 – 0x8F) ─────────────────────────────

static constexpr uint8_t PGL_CMD_BEGIN_FRAME              = 0x80;
static constexpr uint8_t PGL_CMD_DRAW_OBJECT              = 0x81;
static constexpr uint8_t PGL_CMD_SET_CAMERA               = 0x82;
static constexpr uint8_t PGL_CMD_SET_SHADER               = 0x83;

// Programmable shader commands (v0.6)
static constexpr uint8_t PGL_CMD_CREATE_SHADER_PROGRAM    = 0x84;
static constexpr uint8_t PGL_CMD_DESTROY_SHADER_PROGRAM   = 0x85;
static constexpr uint8_t PGL_CMD_BIND_SHADER_PROGRAM      = 0x86;
static constexpr uint8_t PGL_CMD_SET_SHADER_UNIFORM       = 0x87;

static constexpr uint8_t PGL_CMD_END_FRAME                = 0x8F;

// ─── Display Driver Commands (0x90 – 0x9F) ──────────────────────────────────
// Configure display outputs, query capabilities, and set partial-update regions.
// These commands operate on the DisplayManager's registered drivers.

static constexpr uint8_t PGL_CMD_DISPLAY_CONFIGURE        = 0x90;  // Configure display driver
static constexpr uint8_t PGL_CMD_DISPLAY_SET_REGION       = 0x91;  // Set partial update region

// ─── 2D Layer + Drawing Commands (0xA0 – 0xB2) ─────────────────────────────
// Layer lifecycle, 2D primitives, sprites, and compositing configuration.
// Layer 0 is reserved for the 3D scene; layers 1–7 are available for 2D.

// Layer lifecycle (M12)
static constexpr uint8_t PGL_CMD_LAYER_CREATE             = 0xA0;  // Create 2D compositing layer
static constexpr uint8_t PGL_CMD_LAYER_DESTROY            = 0xA1;  // Destroy layer + free FB
static constexpr uint8_t PGL_CMD_LAYER_SET_PROPS          = 0xA2;  // Opacity, blend, offset, clip

// 2D drawing primitives (M12)
static constexpr uint8_t PGL_CMD_DRAW_RECT_2D             = 0xA3;  // Filled/outlined rectangle
static constexpr uint8_t PGL_CMD_DRAW_LINE_2D             = 0xA4;  // Bresenham line
static constexpr uint8_t PGL_CMD_DRAW_CIRCLE_2D           = 0xA5;  // Midpoint circle
static constexpr uint8_t PGL_CMD_DRAW_SPRITE              = 0xA6;  // Textured sprite blit

// Text + batched sprites (M13)
static constexpr uint8_t PGL_CMD_DRAW_TEXT                 = 0xA7;  // Bitmap font text
static constexpr uint8_t PGL_CMD_DRAW_SPRITE_BATCH         = 0xA8;  // Batched sprite array

// Layer operations (M12)
static constexpr uint8_t PGL_CMD_LAYER_CLEAR              = 0xA9;  // Clear layer to solid color

// Additional 2D primitives (M12)
static constexpr uint8_t PGL_CMD_DRAW_ROUNDED_RECT        = 0xAA;  // Rounded-corner rectangle
static constexpr uint8_t PGL_CMD_DRAW_ARC                 = 0xAB;  // Arc segment
static constexpr uint8_t PGL_CMD_DRAW_TRIANGLE_2D         = 0xAC;  // Filled 2D triangle

// Extended layer features (M13)
static constexpr uint8_t PGL_CMD_BILLBOARD_SPRITE         = 0xAD;  // Camera-facing 3D sprite
static constexpr uint8_t PGL_CMD_LAYER_SET_VISIBILITY     = 0xAE;  // Show/hide without destroy

// Advanced 2D features (M13+)
static constexpr uint8_t PGL_CMD_DRAW_GRADIENT_RECT       = 0xAF;  // Linear gradient fill
static constexpr uint8_t PGL_CMD_SET_CLIP_RECT            = 0xB0;  // Per-layer clip rectangle
static constexpr uint8_t PGL_CMD_SET_VIEWPORT             = 0xB1;  // Per-layer viewport transform
static constexpr uint8_t PGL_CMD_SET_LAYER_SHADER         = 0xB2;  // Per-layer PSB shader
