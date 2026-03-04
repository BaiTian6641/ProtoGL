/**
 * @file PglCRC16.h
 * @brief CRC-16/CCITT implementation for ProtoGL frame integrity.
 *
 * Uses polynomial 0x1021, init 0xFFFF (CRC-16/CCITT-FALSE).
 * Compact lookup-free implementation suitable for both ESP32 and RP2350.
 */

#pragma once

#include <cstdint>
#include <cstddef>

class PglCRC16 {
public:
    static uint16_t Compute(const uint8_t* data, size_t length) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < length; ++i) {
            crc ^= static_cast<uint16_t>(data[i]) << 8;
            for (int j = 0; j < 8; ++j) {
                if (crc & 0x8000) {
                    crc = (crc << 1) ^ 0x1021;
                } else {
                    crc <<= 1;
                }
            }
        }
        return crc;
    }

    /// Incremental CRC update (feed byte by byte)
    static uint16_t Update(uint16_t crc, uint8_t byte) {
        crc ^= static_cast<uint16_t>(byte) << 8;
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
        return crc;
    }

    static constexpr uint16_t INIT = 0xFFFF;
};
