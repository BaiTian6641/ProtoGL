/**
 * @file PglCRC16.h
 * @brief CRC-16/CCITT implementation for ProtoGL frame integrity.
 *
 * Uses polynomial 0x1021, init 0xFFFF (CRC-16/CCITT-FALSE).
 * Table-driven MSB-first form: one 256-entry constexpr lookup table is
 * generated at compile time from the same bit recurrence, so results are
 * bit-identical to the original bit-by-bit implementation while costing
 * one table lookup per byte instead of eight shift/XOR rounds.
 * Validated exhaustively against the bit algorithm (all 256 single-byte
 * values, chained updates, "123456789" check vector) in
 * tests/syntax_check/check_crc16.cpp.  Freestanding: no heap, no globals.
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace PglCRC16Detail {

// 256-entry lookup table, built at compile time from the original
// bit-by-bit recurrence: entry[i] = (i << 8) stepped 8 times through
// polynomial 0x1021 (MSB first).
struct CrcTable {
    uint16_t v[256];
    constexpr CrcTable() : v{} {
        for (uint16_t i = 0; i < 256; ++i) {
            uint16_t crc = static_cast<uint16_t>(i << 8);
            for (int j = 0; j < 8; ++j) {
                crc = (crc & 0x8000)
                    ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                    : static_cast<uint16_t>(crc << 1);
            }
            v[i] = crc;
        }
    }
};

}  // namespace PglCRC16Detail

class PglCRC16 {
public:
    static uint16_t Compute(const uint8_t* data, size_t length) {
        uint16_t crc = INIT;
        for (size_t i = 0; i < length; ++i) {
            crc = Update(crc, data[i]);
        }
        return crc;
    }

    /// Incremental CRC update (feed byte by byte)
    static uint16_t Update(uint16_t crc, uint8_t byte) {
        const uint8_t idx = static_cast<uint8_t>((crc >> 8) ^ byte);
        return static_cast<uint16_t>((crc << 8) ^ kTable.v[idx]);
    }

    static constexpr uint16_t INIT = 0xFFFF;

private:
    static constexpr PglCRC16Detail::CrcTable kTable{};
};
