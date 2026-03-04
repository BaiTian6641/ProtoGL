/**
 * @file PglParser.h
 * @brief Alignment-safe command buffer parser utilities for the GPU side.
 *
 * RISC-V cores (and some ARM configurations) may trap on unaligned memory
 * access. This header provides safe reading helpers that use memcpy to avoid
 * relying on hardware unaligned-load support.
 *
 * Usage on the GPU side:
 *   const uint8_t* ptr = ringBuffer + offset;
 *   float x = PglRead<float>(ptr);       // advances ptr by 4
 *   uint16_t id = PglRead<uint16_t>(ptr); // advances ptr by 2
 *
 * Alternatively, use PglReadStruct to deserialize a full packed struct:
 *   PglCmdDrawObject cmd;
 *   PglReadStruct(ptr, cmd);              // advances ptr by sizeof(cmd)
 *
 * All reads are little-endian (wire native). If the GPU core is big-endian
 * (rare for RISC-V), the caller must byte-swap after reading.
 *
 * ProtoGL API Specification v0.3 — FROZEN wire format
 */

#pragma once

#include <cstdint>
#include <cstring>

// ─── Single-Value Read (alignment-safe) ─────────────────────────────────────

/**
 * @brief Read a value of type T from an unaligned byte stream.
 *        Advances the pointer past the read bytes.
 *
 * Safe on all architectures: ARM Cortex-M, RISC-V (RV32/RV64), Xtensa, etc.
 */
template <typename T>
inline T PglRead(const uint8_t*& ptr) {
    T value;
    std::memcpy(&value, ptr, sizeof(T));
    ptr += sizeof(T);
    return value;
}

/**
 * @brief Read a value of type T from a byte pointer without advancing.
 */
template <typename T>
inline T PglPeek(const uint8_t* ptr) {
    T value;
    std::memcpy(&value, ptr, sizeof(T));
    return value;
}

// ─── Struct Read (alignment-safe) ───────────────────────────────────────────

/**
 * @brief Deserialize a packed struct from a byte stream.
 *        Advances the pointer past the struct.
 *
 * The struct must be a POD / trivially-copyable type (all PglCmd* types qualify).
 */
template <typename T>
inline void PglReadStruct(const uint8_t*& ptr, T& out) {
    std::memcpy(&out, ptr, sizeof(T));
    ptr += sizeof(T);
}

/**
 * @brief Deserialize a packed struct from a byte pointer without advancing.
 */
template <typename T>
inline void PglPeekStruct(const uint8_t* ptr, T& out) {
    std::memcpy(&out, ptr, sizeof(T));
}

// ─── Array Read (alignment-safe) ────────────────────────────────────────────

/**
 * @brief Copy N elements of type T from a byte stream into a destination array.
 *        Advances the source pointer past the copied bytes.
 */
template <typename T>
inline void PglReadArray(const uint8_t*& ptr, T* dest, uint16_t count) {
    const size_t bytes = static_cast<size_t>(count) * sizeof(T);
    std::memcpy(dest, ptr, bytes);
    ptr += bytes;
}

// ─── Skip / Advance ────────────────────────────────────────────────────────

/**
 * @brief Advance the read pointer by `bytes` without reading.
 */
inline void PglSkip(const uint8_t*& ptr, size_t bytes) {
    ptr += bytes;
}

// ─── Sync Word Detection ───────────────────────────────────────────────────

/**
 * @brief Scan a byte stream for the PGL_SYNC_WORD (0x55AA, little-endian).
 * @param data   Pointer to the start of the search region.
 * @param length Number of bytes available to search.
 * @return Offset of the sync word, or -1 if not found.
 *
 * Performs a byte-by-byte scan, safe on all architectures.
 */
inline int32_t PglFindSyncWord(const uint8_t* data, size_t length) {
    if (length < 2) return -1;
    for (size_t i = 0; i <= length - 2; ++i) {
        // Little-endian: 0xAA at offset i, 0x55 at offset i+1
        if (data[i] == 0xAA && data[i + 1] == 0x55) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// ─── Frame Validation ──────────────────────────────────────────────────────

/**
 * @brief Validate a complete frame's CRC-16.
 * @param frameStart  Pointer to the first byte of the frame header (sync word).
 * @param totalLength The totalLength field from the frame header (includes CRC).
 * @return true if the CRC matches.
 *
 * Requires PglCRC16.h to be included before this header if you use this function.
 */
#ifdef PGL_CRC16_H_INCLUDED
inline bool PglValidateFrameCRC(const uint8_t* frameStart, uint32_t totalLength) {
    if (totalLength < 14) return false; // header(12) + CRC(2) minimum
    const uint32_t dataLen = totalLength - 2;
    uint16_t computed = PglCRC16::Compute(frameStart, dataLen);
    uint16_t stored;
    std::memcpy(&stored, frameStart + dataLen, 2);
    return computed == stored;
}
#endif

