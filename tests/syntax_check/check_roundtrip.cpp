// ProtoGL wire-format round-trip smoke test — runnable regression gate.
//
// Unlike check.cpp / check_shaders.cpp (-fsyntax-only), this file is compiled,
// linked and EXECUTED on the desktop. It encodes a small frame with PglEncoder,
// finalizes it (header patch + CRC-16 footer), then parses the bytes back with
// the alignment-safe PglParser helpers, verifying:
//   * frame header fields (sync word, frame number, total length, cmd count)
//   * the exact opcode sequence
//   * per-command payload contents
//   * CRC acceptance of the intact frame
//   * CRC rejection of a tampered frame
//
// Together with the static_asserts in PglTypes.h, this is the regression gate
// for wire-format (protocol V8) changes: if the frame layout, command layout
// or CRC algorithm drifts, this test must fail.
//
// Exit code: 0 = all checks passed, 1 = at least one check failed.

#include "../../src/ProtoGL.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

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

bool floatEq(float a, float b) {
    return a == b; // exact: values written and read through the same wire bytes
}

} // namespace

int main() {
    static constexpr uint32_t FRAME_NO   = 0x12345678u;
    static constexpr uint32_t FRAME_US   = 16666u;
    static constexpr PglMesh     MESH_ID = 5;
    static constexpr PglMaterial MAT_ID  = 7;
    static constexpr PglCamera   CAM_ID  = 2;
    static constexpr PglLayout   LAY_ID  = 1;

    static constexpr PglQuat QUAT_IDENTITY = { 1.0f, 0.0f, 0.0f, 0.0f };
    static constexpr PglVec3 VEC3_ZERO     = { 0.0f, 0.0f, 0.0f };
    static constexpr PglVec3 VEC3_ONE      = { 1.0f, 1.0f, 1.0f };
    const PglVec3 drawPos = { 1.5f, -2.5f, 3.5f };

    // ─── Encode ─────────────────────────────────────────────────────────────

    uint8_t buffer[1024];
    PglEncoder enc(buffer, sizeof(buffer));
    enc.BeginFrame(FRAME_NO, FRAME_US);
    enc.SetCamera(CAM_ID, LAY_ID, VEC3_ZERO, QUAT_IDENTITY, VEC3_ONE,
                  QUAT_IDENTITY, QUAT_IDENTITY, true);
    enc.DrawObject(MESH_ID, MAT_ID, drawPos, QUAT_IDENTITY, VEC3_ONE,
                   QUAT_IDENTITY, QUAT_IDENTITY, VEC3_ZERO, VEC3_ZERO, true);
    enc.EndFrame();

    check(!enc.HasOverflow(), "encoder: no overflow");
    check(enc.GetCommandCount() == 4, "encoder: 4 commands recorded");

    const uint8_t* frame = enc.GetBuffer();
    const size_t frameLen = enc.GetLength();

    // ─── Frame header ───────────────────────────────────────────────────────

    PglFrameHeader hdr{};
    PglPeekStruct(frame, hdr);
    check(hdr.syncWord == PGL_SYNC_WORD, "header: sync word 0x55AA");
    check(hdr.frameNumber == FRAME_NO, "header: frame number round-trips");
    check(hdr.totalLength == frameLen, "header: totalLength == encoder length");
    check(hdr.commandCount == 4, "header: commandCount patched to 4");
    check(PglFindSyncWord(frame, frameLen) == 0, "parser: sync word found at 0");

    // ─── CRC acceptance (intact frame) ──────────────────────────────────────

    check(PglValidateFrameCRC(frame, hdr.totalLength),
          "crc: intact frame validates");

    // ─── Command walk: opcode sequence + payloads ───────────────────────────

    static constexpr uint8_t kExpectedOps[4] = {
        PGL_CMD_BEGIN_FRAME,
        PGL_CMD_SET_CAMERA,
        PGL_CMD_DRAW_OBJECT,
        PGL_CMD_END_FRAME,
    };

    const uint8_t* ptr = frame + sizeof(PglFrameHeader);
    const uint8_t* const payloadEnd = frame + hdr.totalLength - sizeof(PglFrameFooter);

    bool opsOk = true;
    bool boundsOk = true;
    for (int i = 0; i < 4; ++i) {
        if (ptr + sizeof(PglCommandHeader) > payloadEnd) { boundsOk = false; break; }
        PglCommandHeader cmd{};
        PglReadStruct(ptr, cmd);
        if (cmd.opcode != kExpectedOps[i]) opsOk = false;
        if (ptr + cmd.payloadLength > payloadEnd) { boundsOk = false; break; }

        switch (cmd.opcode) {
        case PGL_CMD_BEGIN_FRAME: {
            check(cmd.payloadLength == sizeof(PglCmdBeginFrame),
                  "cmd BEGIN_FRAME: payload size");
            PglCmdBeginFrame p{};
            PglReadStruct(ptr, p);
            check(p.frameNumber == FRAME_NO && p.frameTimeUs == FRAME_US,
                  "cmd BEGIN_FRAME: payload fields");
            break;
        }
        case PGL_CMD_SET_CAMERA: {
            check(cmd.payloadLength == sizeof(PglCmdSetCamera),
                  "cmd SET_CAMERA: payload size");
            PglCmdSetCamera p{};
            PglReadStruct(ptr, p);
            check(p.cameraId == CAM_ID && p.pixelLayoutId == LAY_ID && p.is2D == 1,
                  "cmd SET_CAMERA: ids + is2D flag");
            check(floatEq(p.rotation.w, 1.0f) && floatEq(p.scale.x, 1.0f),
                  "cmd SET_CAMERA: quat/scale fields");
            break;
        }
        case PGL_CMD_DRAW_OBJECT: {
            check(cmd.payloadLength == sizeof(PglCmdDrawObject),
                  "cmd DRAW_OBJECT: payload size");
            PglCmdDrawObject p{};
            PglReadStruct(ptr, p);
            check(p.meshId == MESH_ID && p.materialId == MAT_ID,
                  "cmd DRAW_OBJECT: resource ids");
            check((p.flags & PGL_DRAW_ENABLED) != 0 &&
                  (p.flags & PGL_DRAW_VERTEX_OVERRIDE) == 0,
                  "cmd DRAW_OBJECT: flags");
            check(floatEq(p.position.x, drawPos.x) &&
                  floatEq(p.position.y, drawPos.y) &&
                  floatEq(p.position.z, drawPos.z),
                  "cmd DRAW_OBJECT: position floats");
            break;
        }
        case PGL_CMD_END_FRAME: {
            check(cmd.payloadLength == sizeof(PglCmdEndFrame),
                  "cmd END_FRAME: payload size");
            PglCmdEndFrame p{};
            PglReadStruct(ptr, p);
            check(p.frameNumber == FRAME_NO, "cmd END_FRAME: frame echo");
            break;
        }
        default:
            opsOk = false;
            PglSkip(ptr, cmd.payloadLength);
            break;
        }
    }
    check(opsOk, "parser: opcode sequence BEGIN/CAMERA/DRAW/END");
    check(boundsOk, "parser: all payloads inside frame bounds");
    check(ptr == payloadEnd, "parser: consumed exactly up to CRC footer");

    // Footer bytes sit exactly where the walk stopped.
    uint16_t storedCrc = 0;
    std::memcpy(&storedCrc, ptr, sizeof(storedCrc));
    check(storedCrc == PglCRC16::Compute(frame, hdr.totalLength - 2),
          "crc: footer matches recomputed CRC-16/CCITT-FALSE");

    // ─── CRC rejection (tampered frame) ─────────────────────────────────────

    uint8_t tampered[sizeof(buffer)];
    std::memcpy(tampered, frame, frameLen);
    tampered[sizeof(PglFrameHeader) + sizeof(PglCommandHeader)] ^= 0x01; // flip 1 payload bit
    check(!PglValidateFrameCRC(tampered, hdr.totalLength),
          "crc: tampered frame rejected");

    // ─── Summary ────────────────────────────────────────────────────────────

    std::printf("\n");
    if (g_failures == 0) {
        std::printf("RESULT: PASS (wire-format round-trip)\n");
        return 0;
    }
    std::printf("RESULT: FAIL (%d failing check(s))\n", g_failures);
    return 1;
}
