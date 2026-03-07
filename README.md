# ProtoGL Library

**Architecture-agnostic, Vulkan-like graphics command buffer API** for host → GPU rendering pipelines.

## Architecture

The host MCU (e.g., ESP32-S3) records draw commands into a linear byte buffer using `PglEncoder`, then `PglDevice` DMA-transfers the buffer over a high-speed data bus (Octal SPI) to the GPU. The GPU parses the commands, rasterizes the scene, and outputs to the display (HUB75 LED matrices, SPI LCD, etc.).

**The wire protocol is GPU-architecture-agnostic.** The same command bytes work on:
- ARM Cortex-M33 (RP2350 reference implementation)
- RISC-V Hazard3 (RP2350 RISC-V mode)
- Custom RISC-V cores
- FPGAs with soft-core or hardened rasterizer

## Files

| File | Purpose |
|---|---|
| `ProtoGL.h` | Single-include umbrella header |
| `PglTypes.h` | Wire-format structs, enums, resource handles, limits, capability query |
| `PglOpcodes.h` | Command opcode constants (0x01–0x3F, 0x80–0x8F) |
| `PglCRC16.h` | CRC-16/CCITT for frame integrity |
| `PglEncoder.h` | Command buffer encoder (records commands into byte array) — host side |
| `PglParser.h` | Alignment-safe command buffer parser utilities — GPU side (safe on RISC-V) |
| `PglDevice.h` | Device manager (SPI data plane + I2C management bus, DMA transport) — host side |
| `PglShaderBackend.h` | Shader math backend (trig, vector, texture sampling) — GPU side, auto-selects FPv5/DSP |

## Quick Start

```cpp
#include <ProtoGL.h>

PglDevice gpu;
gpu.Initialize({
    .spiClockMHz = 80,
    .i2cAddress = 0x3C,
    .commandBufferSize = 32768,
});

// Query GPU capabilities (architecture, SRAM, limits)
PglCapabilityResponse cap = gpu.QueryCapability();
// cap.gpuArch == PGL_ARCH_ARM_CM33, PGL_ARCH_RISCV_HAZARD3, etc.

// Query detailed GPU health (temp, VRAM, clock, per-core usage)
PglExtendedStatusResponse ext = gpu.QueryExtendedStatus();
// ext.temperatureQ8, ext.currentClockMHz, ext.qspiAVramFreeKB, ...

// Upload mesh once
auto* enc = gpu.GetEncoder();
// ... use encoder directly for resource creation outside frame scope

// Per-frame loop:
gpu.BeginFrame(frameNum, deltaTimeUs);
auto* enc = gpu.GetEncoder();
enc->SetCamera(0, 0, camPos, camRot, camScale, lookOffset, baseRot, false);
enc->DrawObject(meshId, matId, pos, rot, scale, baseRot, sro, so, ro, true);
gpu.EndFrame();  // triggers DMA transfer
```

## GPU-Side Parsing (RISC-V / No Unaligned Access)

```cpp
#include "PglParser.h"

const uint8_t* ptr = receivedBuffer;
PglFrameHeader hdr;
PglReadStruct(ptr, hdr);  // safe memcpy-based read

while (ptr < end) {
    PglCommandHeader cmd;
    PglReadStruct(ptr, cmd);
    switch (cmd.opcode) {
        case PGL_CMD_DRAW_OBJECT: {
            PglCmdDrawObject draw;
            PglReadStruct(ptr, draw);
            // ... process draw call
            break;
        }
        // ...
    }
}
```

## Status

- **v0.6.0** *(planned)* — Programmable shader VM: upload custom shader bytecode (`CreateShaderProgram`), bind to camera slots (`BindShaderProgram`), set uniforms (`SetShaderUniform`). PSB bytecode interpreter on GPU.
- **v0.5.0** — GPU memory access API: 7 new SPI commands (0x30–0x3F) for direct memory read/write across all tiers (SRAM, QSPI-A VRAM, QSPI-B VRAM). 4 new I2C registers (0x0C–0x0F) for tier info, readback, and allocation status. Framebuffer capture for screenshots. Extended status query (32-byte: temp, per-core usage, VRAM, timing). Clock frequency control via I2C. Backward-compatible with v0.3 wire format.
- **v0.4.0** — M4 rasterizer fully implemented on GPU side: vertex transform, perspective/ortho projection, QuadTree spatial indexing, per-pixel barycentric rasterization with Z-buffer, SimpleMaterial evaluation. Dual-core parallel RasterizeRange.
- **v0.3.1** — General shader system: 3 shader classes (CONVOLUTION, DISPLACEMENT, COLOR_ADJUST) replacing 8 hardcoded effects. Animated oscillator waveforms.
- **v0.3.0** — Wire format frozen (backward-compatible with v0.2). Architecture-agnostic: GPU capability query, `PglParser.h` for alignment-safe deserialization, `PglGpuArch` enum.
- Host-side encoder and device fully implemented (M2 complete). GPU firmware transport, display, and rasterizer all implemented.

## Specification

See `docs/ProtoGL_API_Spec.md` for the complete wire-format specification.

## Usage Guide

See `docs/ProtoGL_Usage_And_Examples.md` for a comprehensive, code-first usage and examples reference.
