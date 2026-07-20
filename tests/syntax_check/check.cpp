// ProtoGL syntax/AST compile check — umbrella API smoke TU.
//
// This file is never linked or executed. It exists so that
//   g++ -std=gnu++17 -fsyntax-only -Wall -Wextra
// parses and semantically checks the ProtoGL umbrella header (ProtoGL.h →
// PglTypes/PglOpcodes/PglCRC16/PglEncoder/PglParser/PglDevice) as portable
// C++17, forcing instantiation of the templates and inline code paths an
// ESP32-S3 host build would use. No ARDUINO_ARCH_ESP32 define is set: this
// is the desktop portability gate for the wire format + host API.

#include "../../src/ProtoGL.h"

#include <cstddef>
#include <cstdint>

// Never called — compiled only.
void protogl_api_smoke() {
    // ─── Compile-time wire-format contract ──────────────────────────────
    // The static_asserts in PglTypes.h already fired at include time; keep a
    // few spot checks here so this TU fails loudly if the frozen layout moves.
    static_assert(sizeof(PglFrameHeader) == 12, "frame header layout changed");
    static_assert(sizeof(PglCommandHeader) == 3, "command header layout changed");
    static_assert(sizeof(PglCmdBeginFrame) == 8, "begin-frame payload changed");
    static_assert(sizeof(PglCmdDrawObject) == 101, "draw-object payload changed");
    static_assert(PGL_SYNC_WORD == 0x55AA, "sync word changed");

    // ─── Handle typedefs ────────────────────────────────────────────────
    PglMesh     mesh  = 0;
    PglMaterial mat   = 0;
    PglTexture  tex   = 0;
    PglLayout   lay   = 0;
    PglCamera   cam   = 0;
    PglDisplay  disp  = 0;
    PglLayer    layer = 1;
    (void)tex; (void)disp;

    // ─── Geometry constants (same shapes as examples/hello_triangle) ────
    static constexpr PglQuat QUAT_IDENTITY = { 1.0f, 0.0f, 0.0f, 0.0f };
    static constexpr PglVec3 VEC3_ZERO     = { 0.0f, 0.0f, 0.0f };
    static constexpr PglVec3 VEC3_ONE      = { 1.0f, 1.0f, 1.0f };

    const PglVec3 vertices[3] = {
        {   0.0f,  20.0f, 0.0f },
        { -24.0f, -16.0f, 0.0f },
        {  24.0f, -16.0f, 0.0f },
    };
    const PglIndex3 indices[1] = { { 0, 1, 2 } };
    const PglParamSimple magenta = { 255, 0, 255 };

    // ─── Encoder frame lifecycle into a stack buffer ────────────────────
    uint8_t buffer[4096];
    PglEncoder enc(buffer, sizeof(buffer));

    enc.BeginFrame(1, 16666);

    // Resource creation commands
    enc.CreateMesh(mesh, vertices, 3, indices, 1);
    enc.CreateMaterial(mat, PGL_MAT_SIMPLE, PGL_BLEND_BASE,
                       &magenta, sizeof(magenta));
    const uint16_t texels[4] = { 0xF800, 0x07E0, 0x001F, 0xFFFF };
    enc.CreateTexture(0, 2, 2, PGL_TEX_RGB565, texels);
    const PglVec2 panelSize = { 64.0f, 64.0f };
    const PglVec2 panelPos  = { 0.0f, 0.0f };
    enc.SetPixelLayoutRect(lay, 4096, panelSize, panelPos, 64, 64);

    // Memory-tier commands
    const uint8_t blob[8] = { 0 };
    enc.MemWrite(PGL_TIER_SRAM, 0, blob, sizeof(blob));
    enc.MemAlloc(PGL_TIER_SRAM, 256, 7);
    enc.MemFree(0);
    enc.SetResourceTier(PGL_RES_CLASS_MESH, mesh, PGL_TIER_SRAM, true);

    // Display + 2D layer commands
    enc.DisplayConfigure(0, PGL_DISPLAY_HUB75, 64, 64,
                         PGL_PIXFMT_RGB565, PGL_DISP_FLAG_ENABLED, 64);
    enc.LayerCreate(layer, 64, 64, PGL_PIXFMT_RGB565);
    enc.DrawRect2D(layer, 0, 0, 16, 16, 0xF800, true);
    enc.DrawLine2D(layer, 0, 0, 63, 63, 0x07E0);
    enc.LayerClear(layer);

    // Per-frame rendering commands
    enc.SetCamera(cam, lay, VEC3_ZERO, QUAT_IDENTITY, VEC3_ONE,
                  QUAT_IDENTITY, QUAT_IDENTITY, true);
    enc.DrawObject(mesh, mat, VEC3_ZERO, QUAT_IDENTITY, VEC3_ONE,
                   QUAT_IDENTITY, QUAT_IDENTITY, VEC3_ZERO, VEC3_ZERO, true);
    enc.SetHorizontalBlur(cam, 0, 0.5f, 1);
    enc.ClearShader(cam, 0);

    // Programmable shader commands (v0.6)
    const uint8_t fakeBlob[4] = { 0 };
    enc.CreateShaderProgram(0, fakeBlob, sizeof(fakeBlob));
    enc.BindShaderProgram(cam, 0, 0, 1.0f);
    enc.SetShaderUniform(0, 3, 1.0f);
    enc.SetShaderUniform(0, 4, 1.0f, 0.5f, 0.25f, 0.0f);
    enc.DestroyShaderProgram(0);

    // ─── Protocol v8: capability bits, gen-checked handles, error flags ───
    static_assert(PGL_PROTOCOL_VERSION == 8, "protocol v8");
    static_assert(sizeof(PglCapabilityResponse) == 20, "v8 capability response is 20 bytes");
    static_assert(sizeof(PglExtendedStatusResponse) == 40, "v8 extended status is 40 bytes");
    static_assert(PglMakeHandle(0xAB, 0x12) == 0xAB12, "handle compose is gen|index");
    static_assert(PglHandleIndex(0xAB12) == 0x12, "handle index extraction");
    static_assert(PglHandleGeneration(0xAB12) == 0xAB, "handle generation extraction");
    static_assert(PglHandleIndex(PGL_INVALID_MESH) == PGL_INVALID_HANDLE_INDEX,
                  "invalid sentinel decodes to the reserved invalid index");

    PglCapabilityResponse capv8{};
    capv8.protoVersion = 8;
    capv8.flags   = PGL_CAP_HW_FLOAT;
    capv8.flags16 = static_cast<uint16_t>(
        (PGL_CAP_SHADER_VM | PGL_CAP_HANDLE_GEN | PGL_CAP_ERROR_BITMASK) >> 8);
    (void)PglCapSupports(capv8, PGL_CAP_HW_FLOAT);      // v7 bit via flags
    (void)PglCapSupports(capv8, PGL_CAP_SHADER_VM);     // v8 bit via flags16
    (void)!PglCapSupports(capv8, PGL_CAP_VS_PSB);       // planned bit reads 0
    capv8.protoVersion = 7;
    (void)!PglCapSupports(capv8, PGL_CAP_SHADER_VM);    // v7-safe: bits 8+ invisible

    // Gen-aware resource API (composes gen|index handles, tracks generations)
    PglMesh gm = enc.CreateMeshGen(3, vertices, 3, indices, 1);
    (void)(PglHandleIndex(gm) == 3 && PglHandleGeneration(gm) == 0);
    enc.UpdateVertices(gm, vertices, 3);
    enc.DestroyMeshGen(gm);
    gm = enc.CreateMeshGen(3, vertices, 3, indices, 1);  // re-create bumps gen
    (void)(PglHandleGeneration(gm) == 1);
    enc.DestroyMeshGen(gm);
    PglMaterial gmat = enc.CreateMaterialGen(2, PGL_MAT_SIMPLE, PGL_BLEND_BASE,
                                             nullptr, 0);
    (void)(PglHandleIndex(gmat) == 2);
    enc.DestroyMaterialGen(gmat);
    PglTexture gtex = enc.CreateTextureGen(7, 2, 2, PGL_TEX_RGB565, texels);
    (void)(PglHandleIndex(gtex) == 7);
    (void)(enc.CreateTextureGen(200, 2, 2, PGL_TEX_RGB565, texels) == PGL_INVALID_TEXTURE);
    enc.DestroyTextureGen(gtex);
    enc.ResetGenerations();

    // Parser error flags exist for the firmware's status reporting
    (void)(PGL_PERR_UNKNOWN_OPCODE | PGL_PERR_BAD_LENGTH | PGL_PERR_CRC |
           PGL_PERR_INVALID_HANDLE | PGL_PERR_GEN_MISMATCH |
           PGL_PERR_POOL_EXHAUSTED | PGL_PERR_INVALID_VALUE);

    enc.EndFrame();
    (void)enc.HasOverflow();
    (void)enc.GetLength();
    (void)enc.GetBuffer();
    (void)enc.GetCommandCount();

    // ─── CRC-16 ─────────────────────────────────────────────────────────
    const uint16_t crc  = PglCRC16::Compute(buffer, enc.GetLength());
    const uint16_t crc2 = PglCRC16::Update(PglCRC16::INIT, 0x42);
    (void)crc; (void)crc2;

    // ─── Parser helpers (GPU-side usage pattern) ────────────────────────
    const uint8_t* ptr = buffer;
    const float    f   = PglRead<float>(ptr);
    const uint16_t w   = PglPeek<uint16_t>(ptr);
    PglCommandHeader cmdHdr{};
    PglReadStruct(ptr, cmdHdr);
    PglCmdBeginFrame beginPayload{};
    PglPeekStruct(buffer + sizeof(PglFrameHeader) + sizeof(PglCommandHeader),
                  beginPayload);
    PglSkip(ptr, 4);
    uint8_t raw[8];
    PglReadArray(ptr, raw, 2);
    (void)f; (void)w; (void)raw;

    const int32_t syncOfs = PglFindSyncWord(buffer, sizeof(buffer));
    const bool crcOk = PglValidateFrameCRC(buffer,
                                           static_cast<uint32_t>(enc.GetLength()));
    (void)syncOfs; (void)crcOk;

    // ─── Device manager (host-side lifecycle; desktop no-op transport) ──
    PglDeviceConfig cfg{};
    cfg.spiClkPin = 12;
    cfg.spiCsPin  = 11;
    cfg.i2cAddress = PGL_I2C_DEFAULT_ADDR;

    PglDevice gpu;
    (void)gpu.Initialize(cfg);
    (void)gpu.IsInitialized();
    gpu.BeginFrame(2, 16666);
    PglEncoder* devEnc = gpu.GetEncoder();
    if (devEnc) {
        devEnc->DrawCircle2D(layer, 32, 32, 8, 0x001F, true);
    }
    gpu.EndFrame();
    gpu.SetBrightness(64);
    gpu.SetPanelConfig(64, 64);
    gpu.SetScanRate(16);
    gpu.ClearDisplay();
    gpu.SetGammaTable(0);
    gpu.ResetGPU();
    gpu.SetClockFrequency(250);
    gpu.SetDisplayMode(PGL_DISPLAY_HUB75);
    const PglStatusResponse    st  = gpu.QueryStatus();
    const PglCapabilityResponse cap = gpu.QueryCapability();
    const PglExtendedStatusResponse ext = gpu.QueryExtendedStatus();
    const PglDisplayCaps       dcaps = gpu.QueryDisplayCaps(0);
    const PglMemPoolStatusResponse poolSt = gpu.QueryMemPoolStatus(0);
    const bool hasVram = gpu.HasExternalVram();
    const bool ready   = gpu.IsGpuReady();
    (void)st; (void)cap; (void)ext; (void)dcaps; (void)poolSt;
    (void)hasVram; (void)ready;
    (void)gpu.GetDroppedFrames();
    (void)gpu.GetOverflowFrames();
    (void)gpu.GetGpuStalls();
    (void)gpu.GetConsecutiveDrops();
    gpu.Destroy();
}
