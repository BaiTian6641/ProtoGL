/**
 * @file ProtoGL.h
 * @brief ProtoGL — Vulkan-like Graphics API for ESP32-S3 → RP2350 rendering.
 *
 * Single include header for the ProtoGL host library.
 * Include this file from your ESP32-S3 application code.
 *
 * ProtoGL API Specification v0.3 — FROZEN
 */

#pragma once

#include "PglTypes.h"
#include "PglOpcodes.h"
#include "PglCRC16.h"
#include "PglEncoder.h"
#include "PglParser.h"
#include "PglDevice.h"
