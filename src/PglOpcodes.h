/**
 * @file PglOpcodes.h
 * @brief ProtoGL command opcode definitions.
 *
 * Opcodes 0x01-0x7F: Resource management (create/update/destroy).
 * Opcodes 0x80-0x8F: Per-frame rendering commands.
 *
 * ProtoGL API Specification v0.3 — FROZEN
 */

#pragma once

#include <cstdint>

// ─── Resource Commands (0x01 – 0x7F) ────────────────────────────────────────

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

// ─── Per-Frame Rendering Commands (0x80 – 0x8F) ─────────────────────────────

static constexpr uint8_t PGL_CMD_BEGIN_FRAME              = 0x80;
static constexpr uint8_t PGL_CMD_DRAW_OBJECT              = 0x81;
static constexpr uint8_t PGL_CMD_SET_CAMERA               = 0x82;
static constexpr uint8_t PGL_CMD_SET_EFFECT               = 0x83;
static constexpr uint8_t PGL_CMD_END_FRAME                = 0x8F;
