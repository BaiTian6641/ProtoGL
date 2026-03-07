# ProtoGL Usage & Examples (Comprehensive)

This document is the practical, code-first companion to the protocol spec.

- API status: **ProtoGL v0.5** (host/device + memory commands)
- Wire compatibility baseline: **v0.3 frozen format**
- Host target: ESP32-S3 (`PglDevice`, `PglEncoder`)
- Reference GPU firmware: RP2350

For byte-accurate command layouts, see `docs/ProtoGL_API_Spec.md`.

---

## Table of Contents

1. [Architecture in Practice](#architecture-in-practice)
2. [Quick Start (Minimal Frame)](#quick-start-minimal-frame)
3. [Recommended Frame Loop Pattern](#recommended-frame-loop-pattern)
4. [Resource Upload Patterns](#resource-upload-patterns)
5. [Material Examples](#material-examples)
6. [Camera & Draw Examples](#camera--draw-examples)
7. [Screen-Space Shader Examples](#screen-space-shader-examples)
8. [Programmable Shader Program Examples (v0.6 API)](#programmable-shader-program-examples-v06-api)
9. [GPU Memory Access Examples (v0.5)](#gpu-memory-access-examples-v05)
10. [I2C Control & Health Monitoring](#i2c-control--health-monitoring)
11. [GPUDriverController Integration Examples](#gpudrivercontroller-integration-examples)
12. [Performance & Reliability Guidelines](#performance--reliability-guidelines)
13. [Common Pitfalls](#common-pitfalls)
14. [Reference Limits & Enums](#reference-limits--enums)

---

## Architecture in Practice

Per frame, the host encodes commands into a linear buffer and sends it over Octal SPI:

1. `PglDevice::BeginFrame()`
2. `PglEncoder` writes commands (`SetCamera`, `DrawObject`, etc.)
3. `PglDevice::EndFrame()` finalizes CRC and submits DMA

Control/status is separate over I2C:

- Display config (`SetBrightness`, `SetPanelConfig`, `SetScanRate`, `SetGammaTable`)
- Health query (`QueryStatus`, `QueryCapability`, `QueryExtendedStatus`)
- Clock control (`SetClockFrequency`)

---

## Quick Start (Minimal Frame)

```cpp
#include <ProtoGL.h>

static PglDevice g_gpu;

bool InitGpu() {
    PglDeviceConfig cfg;

    cfg.spiDataPins[0] = 0;
    cfg.spiDataPins[1] = 1;
    cfg.spiDataPins[2] = 2;
    cfg.spiDataPins[3] = 3;
    cfg.spiDataPins[4] = 4;
    cfg.spiDataPins[5] = 5;
    cfg.spiDataPins[6] = 6;
    cfg.spiDataPins[7] = 7;
    cfg.spiClkPin      = 8;
    cfg.spiCsPin       = 9;
    cfg.dirPin         = 10;
    cfg.irqPin         = 13;
    cfg.i2cSdaPin      = 14;
    cfg.i2cSclPin      = 15;

    cfg.spiClockMHz      = 80;
    cfg.i2cAddress       = PGL_I2C_DEFAULT_ADDR; // 0x3C
    cfg.commandBufferSize = 32768;
    cfg.dirTurnaroundCycles = 2;

    if (!g_gpu.Initialize(cfg)) return false;

    PglCapabilityResponse cap = g_gpu.QueryCapability();
    Serial.printf("Proto v%u, arch=%u, cores=%u, sram=%uKB\n",
                  cap.protoVersion, cap.gpuArch, cap.coreCount, cap.sramKB);

    return true;
}
```

---

## Recommended Frame Loop Pattern

Use this split: **static uploads once**, then **frame commands each loop**.

```cpp
struct ProtoIds {
    PglMesh mesh = 0;
    PglMaterial mat = 0;
    PglLayout layout = 0;
    PglCamera cam = 0;
};

static ProtoIds ids;
static uint32_t g_frame = 0;

void UploadStaticResources() {
    PglEncoder* enc = g_gpu.GetEncoder();

    // Mesh
    PglVec3 verts[] = {
        {-0.5f, -0.5f, 0.0f},
        { 0.5f, -0.5f, 0.0f},
        { 0.0f,  0.5f, 0.0f},
    };
    PglIndex3 tris[] = {{0, 1, 2}};
    enc->CreateMesh(ids.mesh, verts, 3, tris, 1);

    // Material
    PglParamSimple red = {255, 0, 0};
    enc->CreateMaterial(ids.mat, PGL_MAT_SIMPLE, PGL_BLEND_REPLACE, &red, sizeof(red));

    // Pixel layout (128x64 panel)
    enc->SetPixelLayoutRect(ids.layout, 128 * 64,
                            PglVec2{128.0f, 64.0f},
                            PglVec2{0.0f, 0.0f},
                            64, 128, false);
}

void RenderFrame(uint32_t nowUs) {
    g_gpu.BeginFrame(g_frame, nowUs);
    PglEncoder* enc = g_gpu.GetEncoder();

    PglVec3 camPos{0.0f, 0.0f, -2.0f};
    PglQuat camRot{1.0f, 0.0f, 0.0f, 0.0f};
    PglVec3 camScale{1.0f, 1.0f, 1.0f};
    PglQuat lookOffset{1.0f, 0.0f, 0.0f, 0.0f};
    PglQuat baseRot{1.0f, 0.0f, 0.0f, 0.0f};

    enc->SetCamera(ids.cam, ids.layout, camPos, camRot, camScale, lookOffset, baseRot, false);

    PglVec3 objPos{0.0f, 0.0f, 0.0f};
    PglQuat objRot{1.0f, 0.0f, 0.0f, 0.0f};
    PglVec3 objScale{1.0f, 1.0f, 1.0f};
    PglQuat scaleRotOffset{1.0f, 0.0f, 0.0f, 0.0f};
    PglVec3 scaleOffset{0.0f, 0.0f, 0.0f};
    PglVec3 rotOffset{0.0f, 0.0f, 0.0f};

    enc->DrawObject(ids.mesh, ids.mat,
                    objPos, objRot, objScale,
                    baseRot, scaleRotOffset, scaleOffset, rotOffset,
                    true);

    g_gpu.EndFrame();
    g_frame++;
}
```

---

## Resource Upload Patterns

### Mesh with UV data

```cpp
PglVec3 verts[] = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
PglIndex3 tris[] = {{0,1,2}, {0,2,3}};
PglVec2 uvVerts[] = {{0,0}, {1,0}, {1,1}, {0,1}};
PglIndex3 uvTris[] = {{0,1,2}, {0,2,3}};

enc->CreateMesh(1, verts, 4, tris, 2,
                true,
                uvVerts, 4,
                uvTris);
```

### Full vertex update

```cpp
enc->UpdateVertices(1, newVerts, newVertexCount);
```

### Sparse delta update

```cpp
PglVertexDelta deltas[] = {
    {5,  0.01f, 0.00f, 0.00f},
    {12, 0.00f, 0.02f, 0.00f},
};
enc->UpdateVerticesDelta(1, deltas, 2);
```

### Texture upload

```cpp
enc->CreateTexture(0, 64, 64, PGL_TEX_RGB565, rgb565Pixels);
```

### Irregular pixel layout

```cpp
enc->SetPixelLayoutIrregular(0, pixelCoordinates, pixelCount, false);
```

---

## Material Examples

### 1) Simple material

```cpp
PglParamSimple p = {255, 128, 0};
enc->CreateMaterial(0, PGL_MAT_SIMPLE, PGL_BLEND_REPLACE, &p, sizeof(p));
```

### 2) Depth material

```cpp
PglParamDepth p{};
p.nearR = 255; p.nearG = 255; p.nearB = 255;
p.farR  = 0;   p.farG  = 0;   p.farB  = 32;
p.nearZ = 0.0f;
p.farZ  = 4.0f;
enc->CreateMaterial(1, PGL_MAT_DEPTH, PGL_BLEND_REPLACE, &p, sizeof(p));
```

### 3) Gradient material

```cpp
struct {
    PglParamGradientHeader hdr;
    PglGradientStop stops[3];
    uint8_t axis;
    float rangeMin;
    float rangeMax;
} p{};

p.hdr.stopCount = 3;
p.stops[0] = {0.0f, 255, 0, 0};
p.stops[1] = {0.5f, 0, 255, 0};
p.stops[2] = {1.0f, 0, 0, 255};
p.axis = 1;
p.rangeMin = -1.0f;
p.rangeMax = 1.0f;

enc->CreateMaterial(2, PGL_MAT_GRADIENT, PGL_BLEND_REPLACE, &p, sizeof(p));
```

### 4) Image material

```cpp
PglParamImage p{};
p.textureId = 0;
p.offsetX = 0.0f;
p.offsetY = 0.0f;
p.scaleX = 1.0f;
p.scaleY = 1.0f;
enc->CreateMaterial(3, PGL_MAT_IMAGE, PGL_BLEND_REPLACE, &p, sizeof(p));
```

### 5) Combine material

```cpp
PglParamCombine p{};
p.materialIdA = 0;
p.materialIdB = 1;
p.blendMode = static_cast<uint8_t>(PGL_BLEND_OVERLAY);
p.opacity = 0.75f;
enc->CreateMaterial(4, PGL_MAT_COMBINE, PGL_BLEND_REPLACE, &p, sizeof(p));
```

---

## Camera & Draw Examples

### Standard draw

```cpp
enc->DrawObject(meshId, materialId,
                pos, rot, scale,
                baseRotation,
                scaleRotationOffset,
                scaleOffset,
                rotationOffset,
                true);
```

### Draw with inline morphed vertices

```cpp
enc->DrawObjectMorphed(meshId, materialId,
                       pos, rot, scale,
                       baseRotation,
                       scaleRotationOffset,
                       scaleOffset,
                       rotationOffset,
                       true,
                       morphedVertices,
                       vertexCount);
```

Use `DrawObjectMorphed` when the host already has per-frame blended geometry and you want to avoid a separate `UpdateVertices` command.

---

## Screen-Space Shader Examples

Shader slots are per camera (`0..3`), max 4 slots per camera.

### Convolution presets

```cpp
enc->SetHorizontalBlur(camId, 0, 1.0f, 3);
enc->SetVerticalBlur(camId,   1, 1.0f, 3);
enc->SetAntiAliasing(camId,   2, 0.8f, 0.25f);
enc->SetRadialBlur(camId,     3, 0.6f, 2, 3.7f);
```

### Displacement presets

```cpp
enc->SetPhaseOffsetX(camId, 0, 1.0f, 4, 3.5f);
enc->SetPhaseOffsetY(camId, 1, 1.0f, 4, 3.5f);
enc->SetPhaseOffsetR(camId, 2, 0.8f, 3, 3.7f, 4.5f, 3.2f);
```

### Color adjust presets

```cpp
enc->SetEdgeFeather(camId, 0, 1.0f, 0.5f);
enc->SetBrightness(camId,  1, 1.0f, 0.1f);
enc->SetContrast(camId,    2, 1.0f, 1.2f);
enc->SetGamma(camId,       3, 1.0f, 2.2f);
```

### Generic shader command

```cpp
PglShaderParamsConvolution p{};
p.kernelShape = PGL_KERNEL_GAUSSIAN;
p.radius = 4;
p.separable = 0;
p.angle = 15.0f;
p.anglePeriod = 0.0f;
p.sigma = 1.0f;

enc->SetShader(camId, 0, PGL_SHADER_CONVOLUTION, 1.0f, &p, sizeof(p));
```

### Clear a slot

```cpp
enc->ClearShader(camId, 0);
```

---

## Programmable Shader Program Examples (v0.6 API)

The host API is available now; firmware support depends on your GPU firmware branch.

```cpp
// Upload bytecode blob (PSB format)
enc->CreateShaderProgram(0, psbBlob, psbSize);

// Bind program to camera 0, slot 1
enc->BindShaderProgram(0, 1, 0, 1.0f);

// Update uniforms
enc->SetShaderUniform(0, 0, 1.0f);                 // float
enc->SetShaderUniform(0, 1, 1.0f, 2.0f);           // vec2
enc->SetShaderUniform(0, 2, 1.0f, 2.0f, 3.0f);     // vec3
enc->SetShaderUniform(0, 3, 1.0f, 2.0f, 3.0f, 4.0f); // vec4

// Unbind or destroy when done
enc->BindShaderProgram(0, 1, 0xFFFF, 0.0f);
enc->DestroyShaderProgram(0);
```

---

## GPU Memory Access Examples (v0.5)

### Write bytes into a tier

```cpp
const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
enc->MemWrite(PGL_TIER_SRAM, 0x20030000, payload, sizeof(payload));
```

### Stage readback from GPU memory

```cpp
enc->MemReadRequest(PGL_TIER_SRAM, 0x20030000, 64);
```

### Set placement hint for an existing resource

```cpp
enc->SetResourceTier(PGL_RES_CLASS_TEXTURE, 0, PGL_TIER_QSPI_A, true);
```

### Allocate/free GPU memory

```cpp
enc->MemAlloc(PGL_TIER_QSPI_A, 4096, 0x1234);
enc->MemFree(handle);
```

### Capture framebuffer

```cpp
enc->FramebufferCapture(0, PGL_TEX_RGB565);
```

### GPU-side memory copy

```cpp
enc->MemCopy(PGL_TIER_SRAM, 0x20030000,
             PGL_TIER_QSPI_A, 0x00100000,
             1024);
```

### Readback path note

`PglDevice` currently exposes encode-side memory commands, but not high-level host helpers for `PGL_REG_MEM_TIER_INFO` / `PGL_REG_MEM_READ_DATA` / `PGL_REG_MEM_ALLOC_RESULT`.

Use your I2C layer (`Wire`/`Wire1`) to:

1. Select register (`0x0C`, `0x0E`, `0x0F`)
2. Read corresponding struct bytes (`PglMemTierInfoResponse`, staged data chunks, `PglMemAllocResult`)

All response structs are defined in `PglTypes.h`.

---

## I2C Control & Health Monitoring

### Display control

```cpp
g_gpu.SetBrightness(128);
g_gpu.SetPanelConfig(128, 64);
g_gpu.SetScanRate(32);
g_gpu.SetGammaTable(2);
```

### Basic status

```cpp
PglStatusResponse st = g_gpu.QueryStatus();
Serial.printf("fps=%u dropped=%u temp=%d flags=0x%02X\n",
              st.currentFPS, st.droppedFrames, st.temperature, st.flags);
```

### Capability query

```cpp
PglCapabilityResponse cap = g_gpu.QueryCapability();
Serial.printf("arch=%u cores=%u maxVerts=%u maxTris=%u flags=0x%02X\n",
              cap.gpuArch, cap.coreCount, cap.maxVertices, cap.maxTriangles, cap.flags);
```

### Extended diagnostics

```cpp
PglExtendedStatusResponse ext = g_gpu.QueryExtendedStatus();
float tempC = ext.temperatureQ8 / 256.0f;

Serial.printf("GPU %u%% C0 %u%% C1 %u%%  %.1fC  %uMHz\n",
              ext.gpuUsagePercent, ext.core0UsagePercent,
              ext.core1UsagePercent, tempC, ext.currentClockMHz);
Serial.printf("frame=%uus raster=%uus xfer=%uus hub75=%uHz\n",
              ext.frameTimeUs, ext.rasterTimeUs,
              ext.transferTimeUs, ext.hub75RefreshHz);
```

### Clock change request

```cpp
g_gpu.SetClockFrequency(250, 0, PGL_CLOCK_RECONFIGURE_PIO);
```

---

## GPUDriverController Integration Examples

`GPUDriverController` is the easiest path for ProtoTracer scenes.

### Basic setup

```cpp
CameraBase* cameras[] = {&cam0, &cam1};
PglDeviceConfig cfg = {/* pins + clocks */};

GPUDriverController controller(cameras, 2, 255, 50, cfg);
controller.Initialize();
```

### Material registration

```cpp
SimpleMaterial red(RGBColor(255, 0, 0));
controller.RegisterSimpleMaterial(&red, 255, 0, 0);

DepthMaterial depthMat;
controller.RegisterDepthMaterial(&depthMat,
                                 255,255,255,
                                 0,0,32,
                                 0.0f, 4.0f);
```

### Frame loop

```cpp
scene.Update(deltaMs);
controller.Render(&scene);
controller.Display();
```

### Controller diagnostics

```cpp
auto& dev = controller.GetDevice();
Serial.printf("dropped=%lu overflow=%lu ready=%d\n",
              (unsigned long)dev.GetDroppedFrames(),
              (unsigned long)dev.GetOverflowFrames(),
              dev.IsGpuReady());

PglExtendedStatusResponse health = controller.QueryGpuHealth();
```

---

## Performance & Reliability Guidelines

1. Keep static resource creation out of the hot frame path.
2. Use `UpdateVerticesDelta` when only a subset of vertices changes.
3. Watch `GetOverflowFrames()`; increase `commandBufferSize` before adding scene complexity.
4. Watch `GetDroppedFrames()` and `GetConsecutiveDrops()`; validate DIR wiring and turnaround cycles.
5. Start with `spiClockMHz = 40` for bring-up, then raise to `64/80` when stable.
6. Poll `QueryExtendedStatus()` at low rate (for example every 30–60 frames), not every loop on busy scenes.

---

## Common Pitfalls

- Calling `DrawObject` with a mesh/material that was never created.
- Mixing coordinate conventions between host scene and camera transform.
- Assuming shader VM is available without checking firmware branch/version.
- Treating I2C memory readback as real-time streaming (it is for debug/inspection).
- Ignoring overflow state after `EndFrame()`.

---

## Reference Limits & Enums

From `PglTypes.h`:

- `PGL_MAX_MESHES = 256`
- `PGL_MAX_MATERIALS = 256`
- `PGL_MAX_TEXTURES = 64`
- `PGL_MAX_IMAGE_SEQUENCES = 32`
- `PGL_MAX_FONTS = 16`
- `PGL_MAX_CAMERAS = 4`
- `PGL_MAX_LAYOUTS = 8`
- `PGL_MAX_DRAW_CALLS = 64`
- `PGL_MAX_VERTICES = 2048`
- `PGL_MAX_TRIANGLES = 1024`
- `PGL_MAX_SHADERS_PER_CAMERA = 4`
- `PGL_MAX_SHADER_PROGRAMS = 16`

Core enum groups to keep nearby when integrating:

- `PglMaterialType`, `PglBlendMode`, `PglTextureFormat`
- `PglShaderClass`, `PglKernelShape`, `PglDisplacementAxis`, `PglColorAdjustOp`, `PglWaveform`
- `PglMemTier`, `PglMemResourceClass`, `PglI2CRegister`
- `PglCapabilityFlags`, `PglStatusFlags`, `PglVramTierFlags`, `PglClockFlags`

---

If you want, I can next add a focused “recipes” appendix (for example: chromatic aberration profile, screenshot capture flow, and dynamic clock governor loop) that you can directly paste into animations.