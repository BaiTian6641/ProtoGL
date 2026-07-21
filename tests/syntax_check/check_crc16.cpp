// ProtoGL CRC-16 table-driven conversion — exhaustive equivalence gate.
//
// PglCRC16 was converted from the bit-by-bit form (8 shift/XOR rounds per
// byte) to a 256-entry constexpr lookup table (F-02).  This test proves the
// two forms are bit-identical by embedding the ORIGINAL bit algorithm as a
// reference and checking:
//   * all 256 single-byte inputs: Compute({i}) == reference({i})
//   * all 256 single-byte Update steps from INIT and from a mid-stream state
//   * a 1 KB pseudo-random buffer: one-shot Compute and byte-chained Update
//     both equal the reference bit algorithm
//   * the canonical CRC-16/CCITT-FALSE check vector "123456789" == 0x29B1
//
// Exit code: 0 = all checks passed, 1 = at least one check failed.

#include "../../src/PglCRC16.h"

#include <cstdint>
#include <cstdio>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) {
        std::printf("PASS %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        ++g_failures;
    }
}

// ─── Reference: the original bit-by-bit implementation (pre-F-02) ───────────

uint16_t RefUpdate(uint16_t crc, uint8_t byte) {
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

uint16_t RefCompute(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc = RefUpdate(crc, data[i]);
    }
    return crc;
}

} // namespace

int main() {
    // ─── 1. All 256 single-byte values: Compute({i}) vs reference ───────────
    {
        bool ok = true;
        for (int i = 0; i < 256; ++i) {
            const uint8_t b = static_cast<uint8_t>(i);
            if (PglCRC16::Compute(&b, 1) != RefCompute(&b, 1)) ok = false;
        }
        check(ok, "all 256 single-byte Compute values match the bit algorithm");
    }

    // ─── 2. All 256 Update steps, from INIT and from a mid-stream state ──────
    {
        bool ok = true;
        for (int i = 0; i < 256; ++i) {
            const uint8_t b = static_cast<uint8_t>(i);
            if (PglCRC16::Update(PglCRC16::INIT, b) != RefUpdate(0xFFFF, b)) ok = false;
            if (PglCRC16::Update(0x3C5A, b) != RefUpdate(0x3C5A, b)) ok = false;
            if (PglCRC16::Update(0x0000, b) != RefUpdate(0x0000, b)) ok = false;
        }
        check(ok, "all 256 Update steps match (INIT / 0x3C5A / 0x0000 states)");
    }

    // ─── 3. 1 KB pseudo-random buffer: Compute and chained Update ────────────
    {
        uint8_t buf[1024];
        uint32_t lfsr = 0x1ACEu;  // nonzero seed
        for (size_t i = 0; i < sizeof(buf); ++i) {
            // 32-bit xorshift PRNG — deterministic across platforms
            lfsr ^= lfsr << 13;
            lfsr ^= lfsr >> 17;
            lfsr ^= lfsr << 5;
            buf[i] = static_cast<uint8_t>(lfsr);
        }

        const uint16_t ref = RefCompute(buf, sizeof(buf));
        check(PglCRC16::Compute(buf, sizeof(buf)) == ref,
              "1 KB pseudo-random buffer: Compute matches reference");

        uint16_t chained = PglCRC16::INIT;
        for (size_t i = 0; i < sizeof(buf); ++i) {
            chained = PglCRC16::Update(chained, buf[i]);
        }
        check(chained == ref,
              "1 KB pseudo-random buffer: byte-chained Update matches reference");

        // Empty input must return INIT unchanged (0-length frames are legal).
        check(PglCRC16::Compute(buf, 0) == PglCRC16::INIT,
              "empty input returns INIT (0xFFFF)");
    }

    // ─── 4. Canonical check vector: "123456789" → 0x29B1 ─────────────────────
    {
        const uint8_t vec[9] = { '1','2','3','4','5','6','7','8','9' };
        check(PglCRC16::Compute(vec, sizeof(vec)) == 0x29B1,
              "check vector \"123456789\" == 0x29B1 (CRC-16/CCITT-FALSE)");
        check(RefCompute(vec, sizeof(vec)) == 0x29B1,
              "reference bit algorithm also yields 0x29B1");
    }

    // ─── Summary ────────────────────────────────────────────────────────────

    std::printf("\n");
    if (g_failures == 0) {
        std::printf("RESULT: PASS (crc16 table equivalence)\n");
        return 0;
    }
    std::printf("RESULT: FAIL (%d failing check(s))\n", g_failures);
    return 1;
}
