/**
 * @file PglOpcodes.h
 * @brief ProtoGL command opcode definitions.
 *
 * Opcodes 0x01-0x2F: Resource management (create/update/destroy).
 * Opcodes 0x30-0x3F: GPU memory access commands.
 * Opcodes 0x80-0x8F: Per-frame rendering commands.
 *
 * ProtoGL API Specification v0.5 — extends v0.3 frozen wire format
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
