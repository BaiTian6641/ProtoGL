# ProtoGL — How to Use

**Vulkan-like graphics command buffer API for ESP32-S3 → GPU rendering offload.**

ProtoGL lets an ESP32-S3 host encode draw commands into a compact binary buffer, DMA them to an external GPU (RP2350 reference), which then rasterizes and drives the display (HUB75 LED matrices, SPI LCD, etc.).

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Architecture Overview](#architecture-overview)
3. [Include & Build Setup](#include--build-setup)
4. [Device Initialization](#device-initialization)
5. [Frame Lifecycle](#frame-lifecycle)
6. [Resource Management](#resource-management)
   - [Meshes](#meshes)
   - [Materials](#materials)
   - [Textures](#textures)
   - [Pixel Layouts](#pixel-layouts)
7. [Camera Setup](#camera-setup)
8. [Draw Calls](#draw-calls)
9. [Shaders (Screen-Space Effects)](#shaders-screen-space-effects)
10. [Programmable Shaders (v0.6)](#programmable-shaders-v06)
11. [GPU Memory Access](#gpu-memory-access)
12. [Resource Persistence](#resource-persistence)
13. [Direct Framebuffer Write](#direct-framebuffer-write)
14. [I2C Configuration](#i2c-configuration)
15. [Extended Status & Diagnostics](#extended-status--diagnostics)
16. [Clock Frequency Control](#clock-frequency-control)
17. [GPUDriverController (ProtoTracer Integration)](#gpudrivercontroller-prototracer-integration)
18. [Error Handling](#error-handling)
19. [Wire Format Reference](#wire-format-reference)
20. [API Reference (Quick)](#api-reference-quick)
21. [Troubleshooting](#troubleshooting)

---

## Quick Start

```cpp
#include <ProtoGL.h>

// 1. Configure the device
PglDeviceConfig cfg;
cfg.spiDataPins[0] = 0;   // D0
cfg.spiDataPins[1] = 1;   // D1
cfg.spiDataPins[2] = 2;   // D2
cfg.spiDataPins[3] = 3;   // D3
cfg.spiDataPins[4] = 4;   // D4
cfg.spiDataPins[5] = 5;   // D5
cfg.spiDataPins[6] = 6;   // D6
cfg.spiDataPins[7] = 7;   // D7
cfg.spiClkPin       = 8;
cfg.spiCsPin        = 9;
cfg.dirPin          = 10;
cfg.irqPin          = 13;
cfg.i2cSdaPin       = 14;
cfg.i2cSclPin       = 15;
cfg.spiClockMHz     = 80;
cfg.i2cAddress      = 0x3C;
cfg.commandBufferSize = 32768;  // 32 KB

// 2. Initialize
PglDevice gpu;
gpu.Initialize(cfg);

// 3. Verify GPU connectivity
PglCapabilityResponse cap = gpu.QueryCapability();
// cap.gpuArch, cap.maxVertices, cap.sramKB, ...

// 4. Upload mesh (once)
PglVec3 verts[] = {{0,0,0}, {1,0,0}, {0,1,0}};
PglIndex3 tris[] = {{0, 1, 2}};
gpu.GetEncoder()->CreateMesh(0, verts, 3, tris, 1);

// 5. Upload material (once)
PglParamSimple matParams = { .r = 255, .g = 0, .b = 0 };
gpu.GetEncoder()->CreateMaterial(0, PGL_MAT_SIMPLE, PGL_BLEND_REPLACE,
                                 &matParams, sizeof(matParams));

// 6. Per-frame rendering loop
uint32_t frame = 0;
void loop() {
    gpu.BeginFrame(frame, micros());
    PglEncoder* enc = gpu.GetEncoder();

    enc->SetCamera(0, 0, camPos, camRot, camScale, lookOffset, baseRot, false);
    enc->DrawObject(0, 0, objPos, objRot, objScale,
                    baseRot, scaleRotOffset, scaleOffset, rotOffset, true);

    gpu.EndFrame();  // appends CRC-16, triggers DMA transfer
    frame++;
}
```

---

## Architecture Overview

```
ESP32-S3 (Host)                        RP2350 (GPU)
┌─────────────────┐                    ┌──────────────────┐
│  ProtoTracer     │                    │  command_parser   │
│  Scene Graph     │                    │  scene_state      │
│       ↓          │                    │       ↓           │
│  PglEncoder      │   Octal SPI       │  pgl_math         │
│  (command buf)   │ ──── DMA ────→    │  Rasterizer       │
│       ↓          │   80 MHz          │       ↓           │
│  PglDevice       │                    │  HUB75 PIO+DMA   │
│  (transport)     │  I2C (mgmt bus)    │  (display output) │
│                  │ ←───────────→     │                   │
└─────────────────┘                    └──────────────────┘
```

**Data flows:**
- **Octal SPI (8-bit parallel, data plane):** High-bandwidth bidirectional bus for rendering command buffers (Host→GPU) and status/memory readback (GPU→Host). Up to 80 MHz clock, ~80 MB/s.
- **I2C (management / control plane):** Low-bandwidth bus for device identification, configuration (brightness, panel, gamma), status monitoring (FPS, temperature, dropped frames), and diagnostics — similar to SMBus on PC platforms. Operates independently of SPI data transfers.
- **DIR pin (GPIO 10):** Host → GPU bus direction control. High = host transmitting, low = GPU transmitting.
- **IRQ pin (GPIO 13):** GPU → host async notification. Active-low, directly usable for ESP-IDF gpio_isr.

---

## Include & Build Setup

### PlatformIO (ESP32-S3)

ProtoGL lives in `lib/ProtoGL/`. PlatformIO auto-discovers it.

```ini
; platformio.ini
[env:esp32s3]
platform = espressif32
board = esp32-s3-devkitm-1
framework = arduino
lib_deps =
    ; ProtoGL is local — no external dependency
```

In your code:
```cpp
#include <ProtoGL.h>
// This pulls in: PglTypes, PglOpcodes, PglCRC16, PglEncoder, PglParser, PglDevice
```

### Files Reference

| File | Purpose |
|---|---|
| `ProtoGL.h` | Single-include umbrella header |
| `PglTypes.h` | Wire-format structs, enums, resource handles, limits |
| `PglOpcodes.h` | Command opcodes (0x01–0x8F) |
| `PglCRC16.h` | CRC-16/CCITT for frame integrity |
| `PglEncoder.h` | Command buffer encoder (records commands into byte array) |
| `PglParser.h` | Alignment-safe parser utilities (GPU-side, RISC-V safe) |
| `PglDevice.h` | Device manager (SPI data plane + I2C management bus, DMA transport) |

---

## Device Initialization

### PglDeviceConfig

```cpp
struct PglDeviceConfig {
    int8_t   spiDataPins[8];       // D0-D7 GPIO pins
    int8_t   spiClkPin;            // SPI clock GPIO
    int8_t   spiCsPin;             // SPI chip select GPIO
    uint8_t  spiClockMHz;          // 40, 64, or 80 MHz
    int8_t   i2cSdaPin;            // I2C data GPIO
    int8_t   i2cSclPin;            // I2C clock GPIO
    uint8_t  i2cAddress;           // Default: 0x3C
    int8_t   rdyPin;               // **DEPRECATED** — use dirPin instead
    int8_t   dirPin;               // Bus direction control (output, high = host TX)
    int8_t   irqPin;               // GPU async interrupt (input, active-low)
    uint32_t commandBufferSize;    // Default: 32768 (32 KB)
    uint8_t  i2cPort;              // 0 = Wire, 1 = Wire1
    uint16_t rdyTimeoutMs;         // **DEPRECATED** — use dirTurnaroundCycles instead
    uint16_t dirTurnaroundCycles;  // Bus turnaround delay (default: 2 cycles)
};
```

### Initialize

```cpp
PglDevice gpu;
bool ok = gpu.Initialize(cfg);
// Allocates double-buffered PSRAM command buffers (ping-pong)
// Configures ESP32-S3 LCD parallel bus (esp_lcd i80) for Octal SPI TX
// Configures SPI2 Octal HD mode for SPI RX (bidirectional readback)
// Configures I2C master at 400 kHz (optional fallback)
// Configures DIR GPIO output and IRQ GPIO input (active-low, ISR-capable)
```

If `Initialize()` returns `false`, check pin assignments and that PSRAM is available.

### Capability Query

After initialization, query the GPU to verify connectivity and learn its limits:

```cpp
PglCapabilityResponse cap = gpu.QueryCapability();
Serial.printf("GPU: arch=%u, cores=%u, sram=%u KB, proto v%u\n",
              cap.gpuArch, cap.coreCount, cap.sramKB, cap.protoVersion);
Serial.printf("Limits: %u verts, %u tris, %u meshes, %u materials\n",
              cap.maxVertices, cap.maxTriangles, cap.maxMeshes, cap.maxMaterials);
```

---

## Frame Lifecycle

Every frame follows a strict sequence:

```
BeginFrame(N, timeUs)
  ├── SetCamera(...)
  ├── DrawObject(...)        × N objects
  ├── DrawObject(...)
  ├── SetShader(...)         (optional)
  └── EndFrame()             → patches header, computes CRC-16, DMA transfers
```

### Using PglDevice (recommended)

```cpp
gpu.BeginFrame(frameNum, micros());
PglEncoder* enc = gpu.GetEncoder();

// ... encode commands via enc ...

gpu.EndFrame();  // CRC + DMA
```

`PglDevice` manages double-buffered command buffers. `BeginFrame()` swaps to the inactive buffer so the previous frame's DMA can still be in-flight.

### Using PglEncoder directly

For lower-level control (e.g., custom transport):

```cpp
uint8_t buffer[32768];
PglEncoder enc(buffer, sizeof(buffer));

enc.BeginFrame(frameNum, deltaUs);
// ... encode commands ...
enc.EndFrame();

if (!enc.HasOverflow()) {
    myTransport.Send(enc.GetBuffer(), enc.GetLength());
}
```

---

## Resource Management

Resources must be created before they can be referenced in draw calls. Resources persist across frames on the GPU until explicitly destroyed.

### Meshes

**Create** (upload vertices + triangles once):

```cpp
PglVec3 vertices[] = {{-1,0,0}, {1,0,0}, {0,1,0}, {0,-1,0}};
PglIndex3 triangles[] = {{0,1,2}, {0,2,3}};

enc->CreateMesh(meshId,           // uint16_t, 0–255
                vertices, 4,      // vertex array + count
                triangles, 2);    // triangle index array + count
```

**With UV coordinates:**

```cpp
PglVec2 uvVerts[] = {{0,0}, {1,0}, {0.5f,1}, {0.5f,0}};
PglIndex3 uvIndices[] = {{0,1,2}, {0,2,3}};

enc->CreateMesh(meshId, verts, 4, tris, 2,
                true,             // hasUV = true
                uvVerts, 4,       // UV vertex array + count
                uvIndices);       // UV index array (same count as triangles)
```

**Update vertices** (morph targets — call per frame when geometry changes):

```cpp
enc->UpdateVertices(meshId, newVertices, vertexCount);
```

**Delta update** (only changed vertices — bandwidth efficient):

```cpp
PglVertexDelta deltas[] = {{vertexIndex, {dx, dy, dz}}, ...};
enc->UpdateVerticesDelta(meshId, deltas, deltaCount);
```

**Destroy:**

```cpp
enc->DestroyMesh(meshId);
```

**Limits:** `PGL_MAX_MESHES` = 256, `PGL_MAX_VERTICES` = 2048 per mesh, `PGL_MAX_TRIANGLES` = 1024 per mesh.

### Materials

**Create:**

```cpp
// Simple solid color
PglParamSimple params = { .r = 255, .g = 128, .b = 0 };
enc->CreateMaterial(matId,             // uint16_t, 0–255
                    PGL_MAT_SIMPLE,
                    PGL_BLEND_REPLACE,
                    &params, sizeof(params));
```

**Material types** (`PglMaterialType`):

| Type | Enum Value | Description |
|---|---|---|
| `PGL_MAT_SIMPLE` | `0x00` | Solid color (RGB) |
| `PGL_MAT_NORMAL` | `0x01` | Surface normal → color mapping |
| `PGL_MAT_DEPTH` | `0x02` | Z-depth with near/far colors |
| `PGL_MAT_GRADIENT` | `0x10` | Position-based gradient with stop array |
| `PGL_MAT_LIGHT` | `0x20` | Diffuse + ambient directional lighting |
| `PGL_MAT_SIMPLEX_NOISE` | `0x30` | Simplex noise with two-color palette |
| `PGL_MAT_RAINBOW_NOISE` | `0x31` | Simplex noise RGB |
| `PGL_MAT_IMAGE` | `0x40` | Texture-mapped (references a PglTexture) |
| `PGL_MAT_IMAGE_SEQUENCE` | `0x41` | Animated texture sequence (references a PglImageSequence — GPU auto-advances frame) |
| `PGL_MAT_COMBINE` | `0x50` | Two-material blend (12 blend modes) |
| `PGL_MAT_MASK` | `0x51` | Threshold-based compositing |
| `PGL_MAT_ANIMATOR` | `0x52` | Time-based material interpolation |
| `PGL_MAT_PRERENDERED` | `0xF0` | Opaque fallback (host pre-rendered) |

**Blend modes** (`PglBlendMode`):

| Mode | Value | Description |
|---|---|---|
| `PGL_BLEND_BASE` | 0 | Base (no blend) |
| `PGL_BLEND_ADD` | 1 | Additive |
| `PGL_BLEND_SUBTRACT` | 2 | Subtractive |
| `PGL_BLEND_MULTIPLY` | 3 | Multiply |
| `PGL_BLEND_DIVIDE` | 4 | Divide |
| `PGL_BLEND_DARKEN` | 5 | Darken (min) |
| `PGL_BLEND_LIGHTEN` | 6 | Lighten (max) |
| `PGL_BLEND_SCREEN` | 7 | Screen |
| `PGL_BLEND_OVERLAY` | 8 | Overlay |
| `PGL_BLEND_SOFTLIGHT` | 9 | Soft light |
| `PGL_BLEND_REPLACE` | 10 | Full replacement |
| `PGL_BLEND_EFFICIENT_MASK` | 11 | Efficient mask |

**Update** (change parameters without recreating):

```cpp
PglParamSimple newParams = { .r = 0, .g = 255, .b = 0 };
enc->UpdateMaterial(matId, &newParams, sizeof(newParams));
```

**Destroy:**

```cpp
enc->DestroyMaterial(matId);
```

### Textures

**Create:**

```cpp
uint16_t pixels[64 * 32];  // RGB565 pixel data
enc->CreateTexture(texId, 64, 32, PGL_TEX_RGB565, pixels);
```

Formats: `PGL_TEX_RGB565` (2 bpp), `PGL_TEX_RGB888` (3 bpp).

**Destroy:**

```cpp
enc->DestroyTexture(texId);
```

**Update** (partial or full pixel replacement without recreating):

```cpp
// Full replacement (same dimensions)
enc->UpdateTexture(texId, 0, 0, 64, 32, newPixels);

// Sub-region update (e.g., refresh a 16×16 patch)
enc->UpdateTexture(texId, 8, 4, 16, 16, patchPixels);
```

**Limits:** `PGL_MAX_TEXTURES` = 64.

**Memory placement:** Small textures (≤ ~4 KB, e.g., 64×32 RGB565 = 4 KB) stay in internal SRAM for single-cycle texel sampling. Larger textures are automatically placed in external VRAM (Tier 1/2) by the tiering manager, with hot cache lines promoted to SRAM on demand.

### Image Sequences (Animated Textures)

An `ImageSequence` stores multiple frames in a single atlas. The GPU auto-advances the active frame based on the specified FPS and loop mode, requiring **zero per-frame bandwidth** from the host after the initial upload. Both 3D materials (`PGL_MAT_IMAGE_SEQUENCE`) and 2D sprites (`CMD_DRAW_SPRITE`) can reference image sequences.

**Create (small sequence — fits in one command buffer):**

```cpp
// 8-frame walk cycle, 16×16 per frame, RGB565
uint16_t frames[16 * 16 * 8];  // all frames sequentially
enc->CreateImageSequence(seqId,       // uint16_t, 0–31
                         16, 16,       // frameWidth, frameHeight
                         8,            // frameCount
                         PGL_TEX_RGB565,
                         PGL_LOOP_LOOP,  // 0=loop, 1=once, 2=ping-pong, 3=hold-last
                         12.0f,          // fps
                         frames);
```

**Create (large sequence — streaming upload across multiple frames):**

```cpp
// 32-frame HD sequence at 64×64 RGB565 = 256 KB total
uint32_t totalSize = 64 * 64 * 2 * 32;

// Frame 1: begin stream
enc->CreateImageSequenceHeader(seqId, 64, 64, 32, PGL_TEX_RGB565,
                               PGL_LOOP_LOOP, 12.0f);
enc->MemStreamBegin(seqHandle, totalSize, 4096);

// Frames 2–N: upload chunks (4 KB each, spread across frames)
for (uint16_t i = 0; i < totalSize / 4096; i++) {
    enc->MemStreamChunk(seqHandle, i, chunkData + i * 4096, 4096);
}

// Final frame: finalize
enc->MemStreamEnd(seqHandle, crc32);
```

**Use with a 3D material:**

```cpp
PglParamImageSequence params = {
    .sequenceId = seqId,
    .offsetX = 0.0f, .offsetY = 0.0f,
    .scaleX = 1.0f, .scaleY = 1.0f,
    .playbackFlags = 0  // bit0: paused, bit1: reverse
};
enc->CreateMaterial(matId, PGL_MAT_IMAGE_SEQUENCE, PGL_BLEND_REPLACE,
                    &params, sizeof(params));
```

**Use with a 2D sprite:**

```cpp
// Draw frame from image sequence as a sprite on a 2D layer
enc->DrawSprite(layerId, seqId | PGL_SPRITE_SRC_SEQUENCE,
                x, y, 16, 16, 0, 0, 0, 0, 255, 0, nullptr);
```

**Destroy:**

```cpp
enc->DestroyImageSequence(seqId);
```

**Limits:** `PGL_MAX_IMAGE_SEQUENCES` = 32.

**Memory placement:** Image sequence atlases are large (KB–MB) and are **always placed in external VRAM** (Tier 1 QSPI-A or Tier 2 QSPI-B, RP2350B only). Only the currently active frame is DMA-fetched into the SRAM cache arena during rasterization. Use `SetResourceTier()` to pin a frequently used sequence to a specific external tier.

### Fonts (Custom Glyph Atlases)

Beyond the two built-in bitmap fonts (5×7 and 8×12), the host can upload custom glyph atlases for use with `CMD_DRAW_TEXT` (fontSize=2). Fonts are shared resources — once uploaded, they work with both 2D text rendering and any future 3D text needs.

**Create:**

```cpp
// Upload a 128×64 grayscale glyph atlas covering ASCII 0x20–0x7E (95 glyphs)
PglGlyphMetrics glyphs[95] = { ... };  // per-glyph atlas coords + metrics
uint8_t atlasPixels[128 * 64];         // GRAYSCALE8 atlas

enc->CreateFont(fontId,           // uint16_t, 0–15
                128, 64,           // atlas width, height
                PGL_TEX_GRAYSCALE8,
                95,                // glyph count
                0x20,              // first ASCII char (space)
                16,                // line height
                12,                // baseline
                PGL_FONT_ANTIALIASED,  // flags
                glyphs,
                atlasPixels);
```

**Use with 2D text:**

```cpp
// fontSize=2 selects custom font; fontId follows the text header
enc->DrawTextCustomFont(layerId, x, y, "Hello!", 6,
                        fontId,      // which uploaded font to use
                        255, 255, 255,  // color
                        0);             // flags
```

**Destroy:**

```cpp
enc->DestroyFont(fontId);
```

**Limits:** `PGL_MAX_FONTS` = 16 (plus 2 built-in fonts always available at indices 0 and 1).

**Memory placement:** Font atlases are read-only after upload. Default tier: external VRAM (prefer MRAM channel). Small fonts (< 2 KB) may remain in SRAM.

### Memory Placement Summary (Materials & Resources)

| Resource Category | Typical Size | Memory Tier | Rationale |
|---|---|---|---|
| **Color-based materials** (Simple, Normal, Depth, Gradient, Light, Noise, Rainbow, Combine, Mask, Animator) | 3–50 bytes | **Always SRAM** (Tier 0) | Trivially small; per-pixel lookups need single-cycle access |
| **Image material params** (ImageMaterial, ImageSequenceMaterial) | 12–13 bytes | **Always SRAM** (Tier 0) | Only the parameter block; backing pixel data is separate |
| **Small textures** (≤ ~4 KB) | ≤ 4 KB | **SRAM** (Tier 0) | Fast texel sampling for icons, small sprites |
| **Large textures** (> 4 KB) | 4 KB – 128 KB | **External VRAM** (Tier 1/2) | Hot cache lines promoted to SRAM cache arena |
| **Image sequence atlases** | KB – MB | **External VRAM** (Tier 1/2) | Only active frame cached in SRAM; too large for on-chip |
| **Font atlases** | 1–16 KB | **External VRAM** (prefer MRAM) | Read-only, benefits from persistent storage |
| **Mesh vertex/index data** | varies | **SRAM → QSPI-A** | Sequential access, DMA-prefetchable |

> **Key rule:** All **color-based and procedural materials** (including `MaterialAnimator` which interpolates between two materials each frame) reside permanently in SRAM — their parameter blocks are tiny (3–50 bytes) and are read every pixel. **Image sequence atlases** are the opposite extreme — they are always placed in external VRAM, with only the active frame promoted to the SRAM cache arena on demand.

### Pixel Layouts

Pixel layouts define how the GPU maps rasterized pixels to physical display coordinates. Set once (or when display configuration changes).

**Rectangular layout** (grid panel):

```cpp
enc->SetPixelLayoutRect(
    layoutId,           // uint8_t, 0–7
    128 * 64,           // total pixel count
    {128.0f, 64.0f},   // size (width, height)
    {0.0f, 0.0f},       // position offset
    64, 128,             // rows, columns
    false                // reversed
);
```

**Irregular layout** (arbitrary pixel positions):

```cpp
PglVec2 pixelCoords[NUM_PIXELS] = { ... };
enc->SetPixelLayoutIrregular(layoutId, pixelCoords, NUM_PIXELS, false);
```

**Limits:** `PGL_MAX_LAYOUTS` = 8.

---

## Camera Setup

Each camera defines the viewpoint and projection mode. Set per frame before draw calls.

```cpp
enc->SetCamera(
    cameraId,           // PglCamera (uint8_t), 0–3
    layoutId,           // PglLayout (uint8_t), 0–7 — which pixel layout to use
    position,           // PglVec3 — world-space position
    rotation,           // PglQuat — orientation
    scale,              // PglVec3 — not typically used for cameras (set to {1,1,1})
    lookOffset,         // PglQuat — additional rotation offset
    baseRotation,       // PglQuat — base rotation (composited: rotation * baseRotation)
    is2D                // bool — true for orthographic, false for perspective
);
```

**Limits:** `PGL_MAX_CAMERAS` = 4.

---

## Draw Calls

Each `DrawObject` tells the GPU to render a mesh with a material and transform.

### Basic Draw

```cpp
enc->DrawObject(
    meshId,                 // PglMesh
    materialId,             // PglMaterial
    position,               // PglVec3    — world-space translation
    rotation,               // PglQuat    — primary rotation
    scale,                  // PglVec3    — per-axis scale
    baseRotation,           // PglQuat    — base rotation (rotation * baseRotation)
    scaleRotationOffset,    // PglQuat    — rotation applied during scaling
    scaleOffset,            // PglVec3    — pivot point for scaling
    rotationOffset,         // PglVec3    — pivot point for rotation
    enabled                 // bool       — set false to skip rendering
);
```

The GPU applies the **7-field transform** in this order:
1. Subtract `scaleOffset` (move to scale pivot)
2. Apply `scale` with `scaleRotationOffset`
3. Add `scaleOffset` back
4. Subtract `rotationOffset` (move to rotation pivot)
5. Apply `rotation * baseRotation`
6. Add `rotationOffset` back
7. Add `position` (translate)

This exactly mirrors ProtoTracer's `Transform::GetTransformMatrix()`.

### Morphed Draw (inline vertex override)

```cpp
enc->DrawObjectMorphed(
    meshId, materialId,
    pos, rot, scale, baseRot, sro, so, ro, true,
    morphedVertices, vertexCount  // override stored vertices for this frame
);
```

This is useful when the host computes a morph blend and sends the final vertex positions inline with the draw call, avoiding a separate `UpdateVertices` command.

**Limits:** `PGL_MAX_DRAW_CALLS` = 64 per frame.

---

## Shaders (Screen-Space Effects)

**`[SHADER:FUTURE]`** — The shader API is designed and encoder methods are complete. GPU firmware has the pipeline implemented. Full integration with ProtoTracer Effect classes requires Effect RTTI (planned for M6).

ProtoGL supports 3 shader classes as post-processing effects per camera:

### Convolution (blur, AA)

```cpp
enc->SetHorizontalBlur(camId, slot, 1.0f, 3);  // radius=3 box blur
enc->SetVerticalBlur(camId, slot, 1.0f, 3);
enc->SetRadialBlur(camId, slot, 0.5f, 2, 3.7f);  // auto-rotating
enc->SetAntiAliasing(camId, slot, 1.0f, 0.25f);   // separable 2D

// Full control:
enc->SetConvolution(camId, slot, intensity,
                    PGL_KERNEL_BOX,    // or PGL_KERNEL_GAUSSIAN
                    radius, separable, angleDeg,
                    anglePeriod, sigma);
```

### Displacement (chromatic aberration, phase offset)

```cpp
enc->SetPhaseOffsetX(camId, slot, 1.0f, 4, 3.5f);   // horizontal
enc->SetPhaseOffsetY(camId, slot, 1.0f, 4, 3.5f);   // vertical
enc->SetPhaseOffsetR(camId, slot, 0.8f, 3);          // radial

// Full control:
enc->SetDisplacement(camId, slot, intensity,
                     PGL_AXIS_X,       // PGL_AXIS_Y, PGL_AXIS_RADIAL
                     perChannel,       // true = chromatic split
                     amplitude, waveform, period, frequency,
                     phase1Period, phase2Period);
```

### Color Adjust (feather, brightness, contrast, gamma)

```cpp
enc->SetEdgeFeather(camId, slot, 1.0f, 0.5f);
enc->SetBrightness(camId, slot, 1.0f, brightnessVal);
enc->SetContrast(camId, slot, 1.0f, contrastVal);
enc->SetGamma(camId, slot, 1.0f, 2.2f);

// Full control:
enc->SetColorAdjust(camId, slot, intensity,
                    PGL_COLOR_EDGE_FEATHER,  // operation enum
                    strength, param2);
```

**Clear a shader slot:**

```cpp
enc->ClearShader(camId, slot);
```

**Shader slots** are per-camera. Each camera can have up to `PGL_MAX_SHADERS_PER_CAMERA` (4) active shaders applied sequentially.

---

## Programmable Shaders (v0.6)

> **Note:** The programmable shader VM is planned for v0.6. The encoder API is complete; GPU-side bytecode interpreter is in development.

Programmable shaders let you upload custom PSB bytecode to the GPU and bind it to camera shader slots, just like built-in shader classes.

### Upload Shader Program

```cpp
// Your compiled PSB bytecode
uint8_t bytecode[] = { 0x01, 0x02, 0x10, ... };

enc->CreateShaderProgram(programId,     // uint16_t, 0–PGL_MAX_SHADER_PROGRAMS-1
                         bytecode,
                         sizeof(bytecode));
```

### Bind to Camera Slot

```cpp
enc->BindShaderProgram(cameraId,       // uint8_t, 0–3
                       shaderSlot,     // uint8_t, 0–3
                       programId,      // uint16_t
                       intensity);     // float, 0.0–1.0
```

### Set Uniform Variables

```cpp
// Set a float uniform
enc->SetShaderUniform(programId, uniformSlot, 1.5f);

// Set a vec2 uniform
enc->SetShaderUniform(programId, uniformSlot, 1.0f, 2.0f);

// Set a vec3 uniform
enc->SetShaderUniform(programId, uniformSlot, 1.0f, 2.0f, 3.0f);

// Set a vec4 uniform
enc->SetShaderUniform(programId, uniformSlot, 1.0f, 2.0f, 3.0f, 4.0f);
```

### Destroy Shader Program

```cpp
enc->DestroyShaderProgram(programId);
```

**Limits:** `PGL_MAX_SHADER_PROGRAMS` = 16.

---

## GPU Memory Access

v0.5 adds direct GPU memory access across all memory tiers. This enables host-initiated reads, writes, allocation, and tier-to-tier copies.

### Memory Tiers

| Tier | Enum | Description |
|---|---|---|
| Tier 0 | `PGL_TIER_SRAM` | GPU on-chip SRAM (520 KB) — fastest |
| Tier 1 | `PGL_TIER_QSPI_A` | QSPI Channel A VRAM (up to 2 chips, RP2350B) |
| Tier 2 | `PGL_TIER_QSPI_B` | QSPI Channel B VRAM (up to 2 chips, RP2350B) |
| Auto | `PGL_TIER_AUTO` | GPU picks best available tier |

### Write to GPU Memory

```cpp
uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
enc->MemWrite(PGL_TIER_SRAM,    // tier
              0x20040000,        // address
              data, sizeof(data));
```

### Read from GPU Memory

Reading is a two-step process — the GPU stages data via SPI, then the host reads it via I2C:

```cpp
// Step 1: Request the GPU to stage data for readback
enc->MemReadRequest(PGL_TIER_SRAM, 0x20040000, 64);

// Step 2 (after frame submission): Read staged data via I2C
uint8_t readBuf[64];
gpu.ReadMemData(readBuf, 64);  // auto-incrementing 32-byte chunks via I2C reg 0x0E
```

### Query Memory Tier Information

```cpp
PglMemTierInfoResponse info = gpu.QueryMemTierInfo();
Serial.printf("SRAM: %u/%u KB free\n", info.sramFreeKB, info.sramTotalKB);
Serial.printf("QSPI-A: %u/%u KB free (enabled=%u, chips=%u)\n",
              info.qspiAFreeKB, info.qspiATotalKB, info.qspiAEnabled, info.qspiAChipCount);
Serial.printf("QSPI-B: %u/%u KB free (enabled=%u, chips=%u)\n",
              info.qspiBFreeKB, info.qspiBTotalKB, info.qspiBEnabled, info.qspiBChipCount);
```

### Allocate GPU Memory

```cpp
// Allocate 4096 bytes in QSPI-A VRAM
enc->MemAlloc(PGL_TIER_QSPI_A,  // tier
              4096,                  // size
              0x0001);               // tag (for tracking)

// After frame: read the allocation result
PglMemAllocResult result = gpu.ReadAllocResult();
if (result.status == PGL_ALLOC_OK) {
    Serial.printf("Allocated handle=%u at 0x%08X\n",
                  result.handle, result.address);
} else {
    Serial.printf("Alloc failed: status=%u\n", result.status);
}
```

### Free GPU Memory

```cpp
enc->MemFree(allocHandle);  // PglMemHandle returned from MemAlloc
```

### Resource Tier Placement

```cpp
// Hint: move mesh 5 to QSPI-A VRAM, pinned
enc->SetResourceTier(PGL_RES_CLASS_MESH,    // resource class
                     5,                      // resource ID
                     PGL_TIER_QSPI_A,        // preferred tier
                     PGL_TIER_FLAG_PINNED);  // flags
```

### Framebuffer Capture

```cpp
// Capture front buffer in RGB565 format for screenshot readback
enc->FramebufferCapture(0,              // 0 = front buffer, 1 = back buffer
                        PGL_TEX_RGB565); // format
// Then use MemReadRequest + ReadMemData to retrieve the pixel data
```

### GPU-Internal Memory Copy

```cpp
// Copy 1024 bytes from SRAM to QSPI-A VRAM
enc->MemCopy(PGL_TIER_SRAM, 0x20040000,       // src tier + addr
             PGL_TIER_QSPI_A, 0x00100000,     // dst tier + addr
             1024);                             // size
```

---

## Resource Persistence

Large resources (textures, image sequence atlases, font atlases) reside in external
VRAM (Tier 1). The GPU auto-detects the VRAM type at boot:

- **MRAM (non-volatile):** Data survives power cycles. Resources are **already persistent** —
  no flash writeback needed. The GPU keeps resource metadata pointing to MRAM. Zero cost.
- **PSRAM (volatile):** Data is lost on power-off. If you want a resource to survive
  reboots, the GPU writes it back to on-chip **flash** (512 KB reserved) in the background.

### Persist a Resource (PSRAM → Flash Writeback)

```cpp
// Upload a large texture (first time — goes to VRAM Tier 1)
enc->CreateTexture(texId, 256, 256, PGL_TEX_RGB565, pixelData, 256*256*2);
enc->EndFrame();  // submit

// Later: tell GPU to persist this texture across reboots
PglEncoder* enc2 = gpu.GetEncoder();
enc2->BeginFrame(frameNum++, micros());
enc2->PersistResource(PGL_RES_TEXTURE, texId,
                      0x00);   // flags: 0 = default (writeback if volatile)
enc2->EndFrame();
// GPU detects PSRAM → copies texture data to flash in background (4 KB/frame)
// GPU detects MRAM → no-op (already persistent)
```

### Restore Resources After Reboot

```cpp
// After power cycle, restore a specific resource from flash
enc->RestoreResource(PGL_RES_TEXTURE, texId,
                     PGL_RESTORE_AUTO_ALLOC);  // auto-allocate matching tier
enc->EndFrame();
// GPU reads flash manifest → allocates VRAM → copies data back
// The texture is now usable again without re-uploading from host
```

### Query Persistence Status

```cpp
// Query a specific resource
enc->QueryPersistence(PGL_RES_TEXTURE, texId,
                      0x00);  // flags: query single resource
enc->EndFrame();

// Read result via I2C register 0x1C (12 bytes) — or use SPI read 0xEB for ~1000× faster access
PglPersistStatusResponse resp;
gpu.ReadI2C(PGL_REG_PERSIST_STATUS, &resp, sizeof(resp));
// resp.status: 0=not persisted, 1=persisting (in progress), 2=persisted, 3=error
// resp.flashAddr, resp.sizeBytes: where/how big

// Or query the full manifest summary
enc->QueryPersistence(0, 0, PGL_PERSIST_QUERY_MANIFEST);
// Returns: totalEntries, usedEntries, freeBytes
```

### Erase Persisted Data

```cpp
// Remove a resource from flash persistence
enc->ErasePersisted(PGL_RES_TEXTURE, texId);
enc->EndFrame();
// Flash entry is freed; resource remains in VRAM until destroyed
```

### Typical Workflow: Upload Once, Boot Instantly

```
First boot:
  Host uploads texture → GPU stores in VRAM (PSRAM or MRAM)
  Host calls PersistResource() → GPU writes to flash (if PSRAM) or no-op (if MRAM)

Subsequent boots:
  Host calls RestoreResource() → GPU loads from flash (if PSRAM) or points to MRAM
  Host skips upload entirely → saves SPI bandwidth + boot time
```

### Persistence Decision Flow

The GPU firmware applies this logic automatically:

1. Is external VRAM present? → No: resource stays in SRAM, cannot persist.
2. Is VRAM type MRAM? → Yes: already non-volatile, mark as persisted, done.
3. VRAM is PSRAM (volatile). Did host request persistence? → No: nothing to do.
4. Host requested persistence → GPU writes resource from PSRAM to flash in background.
5. On next boot → GPU scans flash manifest → auto-detects VRAM type → restores.

---

## Direct Framebuffer Write

The host MCU can write raw pixel data directly to the GPU's output framebuffer,
bypassing the 3D pipeline entirely. This is useful for:

- **Pre-rendered frames** — send camera/video output directly to display
- **Custom 2D rendering** — host does its own 2D drawing, pushes result to GPU
- **Hybrid rendering** — 3D scene on one layer, host-drawn pixels on another
- **Splash screens / boot logos** — display content before full pipeline is ready

### Write Pixels to Output Framebuffer

```cpp
// Write a 32×16 region of pixels starting at (48, 24) on the default output FB
uint16_t pixels[32 * 16];  // RGB565
// ... fill pixels with your image data ...

enc->WriteFramebuffer(48, 24,      // x, y (top-left origin)
                      32, 16,      // width, height
                      0xFF,        // layerId: 0xFF = default output FB
                      pixels,      // pixel data (RGB565)
                      sizeof(pixels));
enc->EndFrame();
```

### Write to a Compositor Layer

```cpp
// Write pixels to compositor Layer 1 (2D overlay)
// Layer must be created first with CMD_LAYER_CREATE
enc->WriteFramebuffer(0, 0,      // x, y
                      128, 64,   // full panel
                      1,         // layerId = 1
                      myLayerPixels,
                      128 * 64 * 2);
enc->EndFrame();
// Compositor blends Layer 1 over Layer 0 (3D) at EndFrame
```

### Interaction with 3D Rendering

If both `DrawObject` and `WriteFramebuffer` appear in the same frame:
1. 3D rasterization runs first (all `DrawObject` commands)
2. Direct framebuffer writes overwrite the specified region
3. If targeting a compositor layer (layerId ≠ 0xFF), the compositor blends normally

### Full-Frame Push (No 3D)

```cpp
// Push an entire pre-rendered frame — no meshes, no camera needed
enc->BeginFrame(frameNum++, micros());
enc->WriteFramebuffer(0, 0, 128, 64, 0xFF, fullFrame, 128*64*2);
enc->EndFrame();
// GPU displays the frame directly — zero 3D overhead
```

---

## I2C Configuration (Management Bus)

I2C serves as the **management / control plane** — small, infrequent register
transactions for configuration and status monitoring (similar to SMBus on PCs).
These commands operate independently of the high-bandwidth Octal SPI data plane:

```cpp
// Brightness (0–255)
gpu.SetBrightness(128);

// Panel resolution (triggers GPU reconfiguration)
gpu.SetPanelConfig(128, 64);

// HUB75 scan rate
gpu.SetScanRate(32);  // 1/32 scan

// Gamma lookup table
gpu.SetGammaTable(2);  // table index

// Clear display to black
gpu.ClearDisplay();

// Reset GPU (re-initializes all state)
gpu.ResetGPU();
```

### Status Query

```cpp
PglStatusResponse status = gpu.QueryStatus();
// status.currentFPS, status.droppedFrames, status.freeMemory16, status.temperature, status.flags
```

---

## Extended Status & Diagnostics

The extended status query returns a 32-byte response with detailed GPU health:

```cpp
PglExtendedStatusResponse ext = gpu.QueryExtendedStatus();

Serial.printf("FPS: %u, Dropped: %u\n", ext.currentFPS, ext.droppedFrames);
Serial.printf("GPU Usage: %u%%, Core0: %u%%, Core1: %u%%\n",
              ext.gpuUsagePercent, ext.core0UsagePercent, ext.core1UsagePercent);
Serial.printf("Temp: %.1f C\n", ext.temperatureQ8 / 256.0f);
Serial.printf("Clock: %u MHz\n", ext.currentClockMHz);
Serial.printf("SRAM Free: %u KB\n", ext.sramFreeKB);
Serial.printf("QSPI-A VRAM: %u/%u KB free\n", ext.qspiAVramFreeKB, ext.qspiAVramTotalKB);
Serial.printf("QSPI-B VRAM: %u/%u KB free\n", ext.qspiBVramFreeKB, ext.qspiBVramTotalKB);
Serial.printf("Timing: frame=%u us, raster=%u us, transfer=%u us\n",
              ext.frameTimeUs, ext.rasterTimeUs, ext.transferTimeUs);
Serial.printf("HUB75 Refresh: %u Hz\n", ext.hub75RefreshHz);
Serial.printf("VRAM Tier Flags: 0x%02X, QSPI-A chips: %u, QSPI-B chips: %u\n",
              ext.vramTierFlags, ext.qspiAChipCount, ext.qspiBChipCount);
```

### PglExtendedStatusResponse Fields

| Field | Type | Description |
|---|---|---|
| `currentFPS` | `uint16_t` | Current render frame rate |
| `droppedFrames` | `uint16_t` | Frames dropped since boot |
| `gpuUsagePercent` | `uint8_t` | Overall GPU busy percentage |
| `core0UsagePercent` | `uint8_t` | Core 0 usage (parser/scene) |
| `core1UsagePercent` | `uint8_t` | Core 1 usage (rasterizer) |
| `flags` | `uint8_t` | `PglStatusFlags` bitfield |
| `temperatureQ8` | `int16_t` | Die temperature in Q8.8 fixed-point (divide by 256 for °C) |
| `currentClockMHz` | `uint16_t` | GPU system clock in MHz |
| `sramFreeKB` | `uint16_t` | Free SRAM in KB |
| `qspiAVramTotalKB` | `uint16_t` | Total QSPI-A VRAM in KB |
| `qspiAVramFreeKB` | `uint16_t` | Free QSPI-A VRAM in KB |
| `qspiBVramTotalKB` | `uint16_t` | Total QSPI-B VRAM in KB |
| `qspiBVramFreeKB` | `uint16_t` | Free QSPI-B VRAM in KB |
| `frameTimeUs` | `uint16_t` | Total frame time in µs |
| `rasterTimeUs` | `uint16_t` | Rasterization time in µs |
| `transferTimeUs` | `uint16_t` | SPI transfer time in µs |
| `hub75RefreshHz` | `uint16_t` | HUB75 display refresh rate |
| `vramTierFlags` | `uint8_t` | VRAM detection/init state (`PglVramTierFlags`) |
| `qspiAChipCount` | `uint8_t` | Number of QSPI-A chips detected (0–2) |
| `qspiBChipCount` | `uint8_t` | Number of QSPI-B chips detected (0–2) |

### Using GPUDriverController Diagnostics

```cpp
// Cached (no I2C round-trip)
float temp = controller.GetGpuTemperature();
uint8_t usage = controller.GetGpuUsagePercent();
uint16_t clock = controller.GetGpuClockMHz();
bool hasVram = controller.HasExternalVram();

// Fresh query (I2C round-trip)
PglExtendedStatusResponse health = controller.QueryGpuHealth();
```

---

## Clock Frequency Control

The GPU clock can be changed at runtime via I2C:

```cpp
// Set GPU to 200 MHz with auto voltage scaling
gpu.SetClockFrequency(200,    // targetMHz (150, 200, 250, 266, 300)
                      0,      // voltageLevel (0 = auto)
                      PGL_CLOCK_RECONFIGURE_PIO);  // re-init PIO timing

// Request thermal auto-management
gpu.SetClockFrequency(300, 0,
                      PGL_CLOCK_RECONFIGURE_PIO | PGL_CLOCK_THERMAL_AUTO);
```

**Clock flags:**

| Flag | Value | Description |
|---|---|---|
| `PGL_CLOCK_RECONFIGURE_PIO` | `0x01` | Reconfigure PIO dividers after clock change |
| `PGL_CLOCK_THERMAL_AUTO` | `0x02` | Enable thermal auto-throttling |

**Via GPUDriverController:**

```cpp
controller.SetGpuClock(250, true);  // 250 MHz with auto voltage
```

---

## GPUDriverController (ProtoTracer Integration)

`GPUDriverController` is a drop-in replacement for controllers like `TasESP32S3KitV1`. It inherits from `Controller` and translates ProtoTracer's scene graph into ProtoGL commands automatically.

### Setup

```cpp
// In your animation header:
#include "Controllers/GPUDriverController.h"

PglDeviceConfig gpuCfg;
gpuCfg.spiDataPins[0] = 0;  // ... set all pins (D0-D7 = GPIO 0-7)
gpuCfg.spiClkPin   = 8;
gpuCfg.spiCsPin    = 9;
gpuCfg.dirPin      = 10;
gpuCfg.irqPin      = 13;
gpuCfg.i2cSdaPin   = 14;
gpuCfg.i2cSclPin   = 15;

CameraBase* cameras[] = { &cam1, &cam2 };
GPUDriverController controller(cameras, 2, 255, 50, gpuCfg);
controller.Initialize();
```

### Material Registration

ProtoTracer's `Material` base class has no RTTI. You must register materials to tell the GPU encoder what type they are:

```cpp
SimpleMaterial redMat(RGBColor(255, 0, 0));
controller.RegisterSimpleMaterial(&redMat, 255, 0, 0);

// Or with full control:
PglParamGradient gradParams = { ... };
controller.RegisterMaterial(&gradMat, PGL_MAT_GRADIENT,
                            &gradParams, sizeof(gradParams));
```

Unregistered materials default to `PGL_MAT_PRERENDERED`.

### Render Loop

```cpp
void loop() {
    scene.Update(deltaTime);
    controller.Render(&scene);   // encodes scene → GPU via DMA
    controller.Display();        // forwards brightness changes; GPU drives HUB75
}
```

**What `Render()` does automatically:**
1. `BeginFrame` with frame number and timestamp
2. Encodes each camera with `SetCamera` + `SetPixelLayout` (first frame only)
3. For each object: creates mesh on first frame, tracks vertex dirty state via FNV-1a hash, only sends `UpdateVertices` when geometry changes
4. Creates materials on first use, encodes `DrawObject` with full 7-field transform
5. `EndFrame` → CRC-16 → DMA transfer

### Diagnostics

```cpp
uint32_t dropped  = controller.GetDevice().GetDroppedFrames();
uint32_t overflow = controller.GetDevice().GetOverflowFrames();
uint32_t frameNum = controller.GetFrameNumber();
bool     ready    = controller.GetDevice().IsGpuReady();
```

---

## Error Handling

### Buffer Overflow

If the encoded commands exceed the buffer capacity, `HasOverflow()` returns true and the frame is **not sent**:

```cpp
gpu.EndFrame();
// Internally: if overflow, increments overflowFrames_ and skips DMA
```

Fix: increase `commandBufferSize` in `PglDeviceConfig`, or reduce the number of objects/vertices per frame.

### Dropped Frames (GPU Busy)

If the GPU is not ready within `dirTurnaroundCycles`, the frame is dropped:

```cpp
uint32_t dropped = gpu.GetDroppedFrames();
```

### CRC Failures (GPU Side)

The GPU validates CRC-16/CCITT on each received frame. On mismatch, the GPU:
- Discards the entire frame
- Continues rendering the last good frame
- Increments its error counter (queryable via I2C status)

### Invalid Resources

Draw calls referencing non-existent mesh/material IDs are silently skipped by the GPU parser.

---

## Wire Format Reference

### Frame Structure

```
┌─────────────────┬─────────────┬─────────────┬───────┬─────┐
│  Frame Header   │  Command 1  │  Command 2  │  ...  │ CRC │
│   (12 bytes)    │  (3+N)      │  (3+N)      │       │ (2) │
└─────────────────┴─────────────┴─────────────┴───────┴─────┘
```

**Frame Header** (12 bytes):
| Field | Type | Description |
|---|---|---|
| `syncWord` | `uint16_t` | Always `0x55AA` |
| `frameNumber` | `uint32_t` | Monotonic frame counter |
| `totalLength` | `uint32_t` | Total bytes (header + commands + CRC) |
| `commandCount` | `uint16_t` | Number of commands in this frame |

**Command** (3 + N bytes):
| Field | Type | Description |
|---|---|---|
| `opcode` | `uint8_t` | Command type (see below) |
| `payloadLength` | `uint16_t` | Payload size in bytes |
| *payload* | *variable* | Opcode-specific data |

**Footer** (2 bytes):
| Field | Type | Description |
|---|---|---|
| `crc16` | `uint16_t` | CRC-16/CCITT over entire frame (header + commands) |

### Opcodes

| Opcode | Name | Description |
|---|---|---|
| `0x01` | `CMD_CREATE_MESH` | Upload mesh (vertices + triangles + optional UV) |
| `0x02` | `CMD_DESTROY_MESH` | Free mesh resources |
| `0x03` | `CMD_UPDATE_VERTICES` | Replace all vertex positions |
| `0x04` | `CMD_UPDATE_VERTICES_DELTA` | Partial vertex update (sparse deltas) |
| `0x10` | `CMD_CREATE_MATERIAL` | Create material with type + blend mode + params |
| `0x11` | `CMD_UPDATE_MATERIAL` | Update material parameters |
| `0x12` | `CMD_DESTROY_MATERIAL` | Free material slot |
| `0x18` | `CMD_CREATE_TEXTURE` | Upload texture (RGB565 or RGB888) |
| `0x19` | `CMD_DESTROY_TEXTURE` | Free texture slot |
| `0x1A` | `CMD_UPDATE_TEXTURE` | Partial or full texture pixel data replacement |
| `0x1B` | `CMD_CREATE_IMAGE_SEQUENCE` | Upload multi-frame animated texture atlas |
| `0x1C` | `CMD_DESTROY_IMAGE_SEQUENCE` | Free image sequence slot |
| `0x1D` | `CMD_CREATE_FONT` | Upload custom glyph atlas with metrics |
| `0x1E` | `CMD_DESTROY_FONT` | Free font slot |
| `0x20` | `CMD_SET_PIXEL_LAYOUT` | Define display pixel mapping |
| `0x30` | `CMD_MEM_WRITE` | Write raw bytes to GPU memory tier |
| `0x31` | `CMD_MEM_READ_REQUEST` | Stage GPU memory for SPI readback (or I2C fallback) |
| `0x32` | `CMD_MEM_SET_RESOURCE_TIER` | Set preferred memory tier for a resource |
| `0x33` | `CMD_MEM_ALLOC` | Allocate region in GPU memory tier |
| `0x34` | `CMD_MEM_FREE` | Free GPU memory allocation |
| `0x35` | `CMD_FRAMEBUFFER_CAPTURE` | Snapshot framebuffer for readback |
| `0x36` | `CMD_MEM_COPY` | GPU-internal tier-to-tier memory copy |
| `0x45` | `CMD_WRITE_FRAMEBUFFER` | Write raw pixels directly to output FB or compositor layer (v0.7.1) |
| `0x46` | `CMD_PERSIST_RESOURCE` | Persist resource to flash (or no-op if MRAM) (v0.7.1) |
| `0x47` | `CMD_RESTORE_RESOURCE` | Restore resource from flash/MRAM after reboot (v0.7.1) |
| `0x48` | `CMD_QUERY_PERSISTENCE` | Query persistence status of a resource or manifest (v0.7.1) |
| `0x80` | `CMD_BEGIN_FRAME` | Start new frame (frame number + timestamp) |
| `0x81` | `CMD_DRAW_OBJECT` | Render mesh with material and transform |
| `0x82` | `CMD_SET_CAMERA` | Set camera position/rotation/projection |
| `0x83` | `CMD_SET_SHADER` | Apply screen-space post-processing effect |
| `0x84` | `CMD_CREATE_SHADER_PROGRAM` | Upload shader program bytecode (v0.6) |
| `0x85` | `CMD_DESTROY_SHADER_PROGRAM` | Destroy shader program (v0.6) |
| `0x86` | `CMD_BIND_SHADER_PROGRAM` | Bind program to camera slot (v0.6) |
| `0x87` | `CMD_SET_SHADER_UNIFORM` | Set program uniform (v0.6) |
| `0x8F` | `CMD_END_FRAME` | End frame (triggers rasterization) |

All structs are **packed, little-endian**. The GPU parser uses `memcpy`-based reads for alignment safety on RISC-V targets.

---

## API Reference (Quick)

### PglDevice

| Method | Description |
|---|---|
| `Initialize(config)` | Set up SPI, I2C, allocate buffers |
| `Destroy()` | Free all resources |
| `GetEncoder()` | Get the current frame's encoder |
| `BeginFrame(num, timeUs)` | Start encoding a new frame |
| `EndFrame()` | Finalize, CRC, DMA transfer |
| `SetBrightness(val)` | I2C: set display brightness (0–255) |
| `SetPanelConfig(w, h)` | I2C: set panel resolution |
| `SetScanRate(rate)` | I2C: set HUB75 scan rate |
| `SetGammaTable(idx)` | I2C: select gamma table |
| `ClearDisplay()` | I2C: clear to black |
| `ResetGPU()` | I2C: full GPU reset |
| `QueryStatus()` | I2C: read GPU status (8 bytes) |
| `QueryCapability()` | SPI read (or I2C fallback): read GPU capabilities (16 bytes) |
| `QueryExtendedStatus()` | SPI read (or I2C fallback): read extended GPU status (32 bytes) |
| `SetClockFrequency(mhz, v, f)` | SPI command (or I2C fallback): change GPU clock frequency |
| `HasExternalVram()` | True if external VRAM detected |
| `QueryMemTierInfo()` | SPI read (or I2C fallback): read per-tier memory stats |
| `ReadMemData(buf, len)` | SPI read: read staged memory via bidirectional bus (or I2C 32B chunks as fallback) |
| `ReadAllocResult()` | SPI read: read last allocation result (or I2C 0x0F as fallback) |
| `IsGpuReady()` | Check DIR pin / SPI status |
| `GetDroppedFrames()` | Frames dropped (GPU busy) |
| `GetOverflowFrames()` | Frames skipped (buffer overflow) |
| `GetGpuStalls()` | Consecutive-drop stall events |
| `GetConsecutiveDrops()` | Running count of sequential drops |

### PglEncoder

| Method | Description |
|---|---|
| `BeginFrame(num, timeUs)` | Write frame header + begin command |
| `EndFrame()` | Write end command, patch header, append CRC |
| `SetCamera(...)` | Set camera for this frame |
| `DrawObject(...)` | Draw mesh with material and transform |
| `DrawObjectMorphed(...)` | Draw with inline vertex override |
| `CreateMesh(...)` | Upload mesh geometry |
| `DestroyMesh(id)` | Free mesh |
| `UpdateVertices(...)` | Replace vertex positions |
| `UpdateVerticesDelta(...)` | Sparse vertex update |
| `CreateMaterial(...)` | Create material resource |
| `UpdateMaterial(...)` | Update material params |
| `DestroyMaterial(id)` | Free material |
| `CreateTexture(...)` | Upload texture |
| `UpdateTexture(...)` | Update texture pixel data (partial or full) |
| `DestroyTexture(id)` | Free texture |
| `CreateImageSequence(...)` | Upload multi-frame animated texture atlas |
| `DestroyImageSequence(id)` | Free image sequence |
| `CreateFont(...)` | Upload custom glyph atlas with metrics |
| `DestroyFont(id)` | Free font |
| `SetPixelLayoutRect(...)` | Define rectangular pixel grid |
| `SetPixelLayoutIrregular(...)` | Define arbitrary pixel positions |
| `SetShader(...)` | Generic shader command |
| `SetConvolution(...)` | Convolution shader (blur/AA) |
| `SetDisplacement(...)` | Displacement shader (chromatic) |
| `SetColorAdjust(...)` | Color adjustment shader |
| `ClearShader(camId, slot)` | Remove shader from slot |
| `SetHorizontalBlur(...)` | Convenience: horizontal box blur |
| `SetVerticalBlur(...)` | Convenience: vertical box blur |
| `SetRadialBlur(...)` | Convenience: radial auto-rotating blur |
| `SetAntiAliasing(...)` | Convenience: separable 2D AA |
| `SetPhaseOffsetX(...)` | Convenience: horizontal phase displacement |
| `SetPhaseOffsetY(...)` | Convenience: vertical phase displacement |
| `SetPhaseOffsetR(...)` | Convenience: radial phase displacement |
| `SetEdgeFeather(...)` | Convenience: edge feathering |
| `SetBrightness(...)` | Convenience: brightness adjustment |
| `SetContrast(...)` | Convenience: contrast adjustment |
| `SetGamma(...)` | Convenience: gamma correction |
| `CreateShaderProgram(...)` | Upload shader bytecode (v0.6) |
| `DestroyShaderProgram(id)` | Destroy shader program (v0.6) |
| `BindShaderProgram(...)` | Bind program to camera slot (v0.6) |
| `SetShaderUniform(...)` | Set program uniform (v0.6) |
| `MemWrite(tier, addr, data, size)` | Write raw bytes to GPU memory tier |
| `MemReadRequest(tier, addr, size)` | Stage GPU memory for SPI readback (or I2C fallback) |
| `SetResourceTier(class, id, tier, pinned)` | Set preferred tier for a resource |
| `MemAlloc(tier, size, tag)` | Allocate region in GPU memory tier |
| `MemFree(handle)` | Free a GPU memory allocation |
| `FramebufferCapture(buf, fmt)` | Snapshot framebuffer for readback |
| `MemCopy(srcTier, srcAddr, dstTier, dstAddr, size)` | GPU-internal memory copy |
| `WriteFramebuffer(x, y, w, h, layerId, pixels, size)` | Write raw pixels to output FB or compositor layer (v0.7.1) |
| `PersistResource(resClass, resId, flags)` | Persist resource to flash or MRAM (v0.7.1) |
| `RestoreResource(resClass, resId, flags)` | Restore resource from flash/MRAM after restart (v0.7.1) |
| `QueryPersistence(resClass, resId, flags)` | Query persistence status (v0.7.1) |
| `ErasePersisted(resClass, resId)` | Remove resource from flash persistence (v0.7.1) |
| `HasOverflow()` | Check if buffer capacity exceeded |
| `GetLength()` | Current encoded byte count |
| `GetBuffer()` | Pointer to encoded data |

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| `Initialize()` returns false | Pin misconfiguration or no PSRAM | Check pin assignments; ensure PSRAM is enabled in board config |
| `QueryCapability()` returns zeros | I2C not connected or wrong address (if using I2C fallback), or SPI bus issue | Verify wiring; check `i2cAddress` matches GPU firmware |
| Frames always dropped | DIR pin not wired or GPU not running | Check DIR wiring (GPIO 10); set `dirPin = -1` to disable flow control temporarily |
| Buffer overflow on complex scenes | Too many vertices/objects for buffer | Increase `commandBufferSize` (e.g., 65536 for 64 KB) |
| GPU shows last frame repeatedly | CRC mismatch — data corruption on bus | Check signal integrity; reduce `spiClockMHz` to 40 |
| Visual artifacts / wrong transform | Transform field order mismatch | Ensure all 7 transform fields are populated correctly |
| `QueryExtendedStatus()` returns zeros | GPU firmware < v0.5 or bus not connected | Update GPU firmware; verify wiring |
| `HasExternalVram()` returns false | No VRAM chips detected at boot | Check VRAM wiring; GPIO pins for QSPI-A/QSPI-B (RP2350B only) |
| `SetClockFrequency()` has no effect | GPU does not support dynamic clock | Check `PGL_CAP_DYNAMIC_CLOCK` in capability flags |
| Memory alloc returns `PGL_ALLOC_TIER_DISABLED` | Requested tier has no VRAM hardware | Use `QueryMemTierInfo()` to check available tiers |

---

*ProtoGL API v0.5 — Extends v0.3 frozen wire format with GPU memory access commands*
