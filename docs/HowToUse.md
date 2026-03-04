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
10. [I2C Configuration](#i2c-configuration)
11. [GPUDriverController (ProtoTracer Integration)](#gpudrivercontroller-prototracer-integration)
12. [Error Handling](#error-handling)
13. [Wire Format Reference](#wire-format-reference)
14. [API Reference (Quick)](#api-reference-quick)

---

## Quick Start

```cpp
#include <ProtoGL.h>

// 1. Configure the device
PglDeviceConfig cfg;
cfg.spiDataPins[0] = 6;   // D0
cfg.spiDataPins[1] = 7;   // D1
cfg.spiDataPins[2] = 8;   // D2
cfg.spiDataPins[3] = 9;   // D3
cfg.spiDataPins[4] = 10;  // D4
cfg.spiDataPins[5] = 11;  // D5
cfg.spiDataPins[6] = 12;  // D6
cfg.spiDataPins[7] = 13;  // D7
cfg.spiClkPin       = 14;
cfg.spiCsPin        = 15;
cfg.rdyPin          = 16;
cfg.i2cSdaPin       = 17;
cfg.i2cSclPin       = 18;
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
│  (transport)     │   I2C (config)    │  (display output) │
│                  │ ←───────────→     │                   │
└─────────────────┘                    └──────────────────┘
```

**Data flows:**
- **Octal SPI (8-bit parallel):** Command buffers from host → GPU every frame. Up to 80 MHz clock.
- **I2C:** Configuration (brightness, panel config, gamma) and status queries. Bidirectional.
- **RDY pin:** GPU → host flow control. High = ready to receive, low = busy.

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
| `PglDevice.h` | Device manager (buffer lifecycle, DMA transport, I2C config) |

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
    int8_t   rdyPin;               // GPU ready signal (input, active high)
    uint32_t commandBufferSize;    // Default: 32768 (32 KB)
    uint8_t  i2cPort;              // 0 = Wire, 1 = Wire1
    uint16_t rdyTimeoutMs;         // Default: 5 ms
};
```

### Initialize

```cpp
PglDevice gpu;
bool ok = gpu.Initialize(cfg);
// Allocates double-buffered PSRAM command buffers (ping-pong)
// Configures ESP32-S3 LCD parallel bus (esp_lcd i80) for Octal SPI
// Configures I2C master at 400 kHz
// Configures RDY GPIO input with pull-down
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

| Type | Description |
|---|---|
| `PGL_MAT_SIMPLE` | Solid color |
| `PGL_MAT_GRADIENT` | Position-based gradient with stop array |
| `PGL_MAT_RAINBOW_NOISE` | Simplex noise RGB |
| `PGL_MAT_NORMAL` | Surface normal → color mapping |
| `PGL_MAT_DEPTH` | Z-depth → brightness |
| `PGL_MAT_LIGHT` | Diffuse + ambient directional lighting |
| `PGL_MAT_COMBINE` | Two-material blend (12 blend modes) |
| `PGL_MAT_ANIMATED` | Time-based material interpolation |
| `PGL_MAT_MASK` | Threshold-based compositing |
| `PGL_MAT_IMAGE` | Texture-mapped (references a PglTexture) |
| `PGL_MAT_PRERENDERED` | Opaque fallback (host pre-rendered) |
| `PGL_MAT_CUSTOM` | User-defined |

**Blend modes** (`PglBlendMode`):

`PGL_BLEND_REPLACE`, `PGL_BLEND_ADD`, `PGL_BLEND_MULTIPLY`, `PGL_BLEND_SCREEN`, `PGL_BLEND_OVERLAY`, `PGL_BLEND_SOFTLIGHT`, and more.

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

**Limits:** `PGL_MAX_TEXTURES` = 64.

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

**Shader slots** are per-camera. Each camera can have multiple active shaders applied sequentially.

---

## I2C Configuration

Configuration commands go over I2C (separate from the SPI data bus):

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
// status.currentFrame, status.errorCount, etc.
```

---

## GPUDriverController (ProtoTracer Integration)

`GPUDriverController` is a drop-in replacement for controllers like `TasESP32S3KitV1`. It inherits from `Controller` and translates ProtoTracer's scene graph into ProtoGL commands automatically.

### Setup

```cpp
// In your animation header:
#include "Controllers/GPUDriverController.h"

PglDeviceConfig gpuCfg;
gpuCfg.spiDataPins[0] = 6;  // ... set all pins
gpuCfg.spiClkPin   = 14;
gpuCfg.spiCsPin    = 15;
gpuCfg.rdyPin      = 16;
gpuCfg.i2cSdaPin   = 17;
gpuCfg.i2cSclPin   = 18;

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

If the GPU's RDY pin is not asserted within `rdyTimeoutMs`, the frame is dropped:

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
| `0x20` | `CMD_SET_PIXEL_LAYOUT` | Define display pixel mapping |
| `0x80` | `CMD_BEGIN_FRAME` | Start new frame (frame number + timestamp) |
| `0x81` | `CMD_DRAW_OBJECT` | Render mesh with material and transform |
| `0x82` | `CMD_SET_CAMERA` | Set camera position/rotation/projection |
| `0x83` | `CMD_SET_SHADER` | Apply screen-space post-processing effect |
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
| `QueryStatus()` | I2C: read GPU status |
| `QueryCapability()` | I2C: read GPU capabilities |
| `IsGpuReady()` | Check RDY pin state |
| `GetDroppedFrames()` | Frames dropped (GPU busy) |
| `GetOverflowFrames()` | Frames skipped (buffer overflow) |

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
| `DestroyTexture(id)` | Free texture |
| `SetPixelLayoutRect(...)` | Define rectangular pixel grid |
| `SetPixelLayoutIrregular(...)` | Define arbitrary pixel positions |
| `SetShader(...)` | Generic shader command |
| `SetConvolution(...)` | Convolution shader (blur/AA) |
| `SetDisplacement(...)` | Displacement shader (chromatic) |
| `SetColorAdjust(...)` | Color adjustment shader |
| `ClearShader(camId, slot)` | Remove shader from slot |
| `HasOverflow()` | Check if buffer capacity exceeded |
| `GetLength()` | Current encoded byte count |
| `GetBuffer()` | Pointer to encoded data |

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| `Initialize()` returns false | Pin misconfiguration or no PSRAM | Check pin assignments; ensure PSRAM is enabled in board config |
| `QueryCapability()` returns zeros | I2C not connected or wrong address | Verify I2C wiring, check `i2cAddress` matches GPU firmware |
| Frames always dropped | RDY pin not wired or GPU not running | Check RDY wiring; set `rdyPin = -1` to disable flow control temporarily |
| Buffer overflow on complex scenes | Too many vertices/objects for buffer | Increase `commandBufferSize` (e.g., 65536 for 64 KB) |
| GPU shows last frame repeatedly | CRC mismatch — data corruption on bus | Check signal integrity; reduce `spiClockMHz` to 40 |
| Visual artifacts / wrong transform | Transform field order mismatch | Ensure all 7 transform fields are populated correctly |

---

*ProtoGL API v0.3 — Specification FROZEN*
