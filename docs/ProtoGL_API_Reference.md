# ProtoGL API Reference

> **Version:** 0.7 &nbsp;|&nbsp; **Generated for:** ProtoGL host library (ESP32-S3) + shared GPU types  
> **Wire compatibility baseline:** v0.3 frozen format  
> **Companion documents:** `docs/ProtoGL_API_Spec.md`, `docs/ProtoGL_Usage_And_Examples.md`

---

## Table of Contents

1. [Overview](#1-overview)
2. [ProtoGL.h — Umbrella Header](#2-protoglh--umbrella-header)
3. [PglTypes.h — Types, Enumerations & Constants](#3-pgltypesh--types-enumerations--constants)
   - 3.1 [Resource Handle Types](#31-resource-handle-types)
   - 3.2 [Dimension & Limit Constants](#32-dimension--limit-constants)
   - 3.3 [Wire Protocol Constants](#33-wire-protocol-constants)
   - 3.4 [Enumerations](#34-enumerations)
   - 3.5 [Wire-Format Structures](#35-wire-format-structures)
   - 3.6 [I2C Response Structures](#36-i2c-response-structures)
4. [PglOpcodes.h — Command Opcodes](#4-pglopcodesh--command-opcodes)
5. [PglCRC16.h — CRC-16/XMODEM](#5-pglcrc16h--crc-16xmodem)
6. [PglEncoder.h — Command Buffer Encoder](#6-pglencoderh--command-buffer-encoder)
   - 6.1 [Construction & State](#61-construction--state)
   - 6.2 [Frame Lifecycle](#62-frame-lifecycle)
   - 6.3 [Camera Commands](#63-camera-commands)
   - 6.4 [Draw Commands](#64-draw-commands)
   - 6.5 [Built-in Shaders — General](#65-built-in-shaders--general)
   - 6.6 [Built-in Shaders — Convolution](#66-built-in-shaders--convolution)
   - 6.7 [Built-in Shaders — Displacement](#67-built-in-shaders--displacement)
   - 6.8 [Built-in Shaders — Color Adjust](#68-built-in-shaders--color-adjust)
   - 6.9 [Programmable Shaders (v0.6)](#69-programmable-shaders-v06)
   - 6.10 [Mesh Resources](#610-mesh-resources)
   - 6.11 [Material Resources](#611-material-resources)
   - 6.12 [Texture Resources](#612-texture-resources)
   - 6.13 [Pixel Layout](#613-pixel-layout)
   - 6.14 [GPU Memory Access](#614-gpu-memory-access)
7. [PglDevice.h — Host Device Driver](#7-pgldeviceh--host-device-driver)
   - 7.1 [PglDeviceConfig](#71-pgldeviceconfig)
   - 7.2 [PglDevice Lifecycle](#72-pgldevice-lifecycle)
   - 7.3 [Frame Lifecycle](#73-frame-lifecycle)
   - 7.4 [I2C Configuration](#74-i2c-configuration)
   - 7.5 [Frame Statistics](#75-frame-statistics)
8. [PglParser.h — Command Buffer Parser](#8-pglparserh--command-buffer-parser)
9. [PglShaderBackend.h — Shader Math Backend](#9-pglshaderbackendh--shader-math-backend)
   - 9.1 [Compile-Time Defines](#91-compile-time-defines)
   - 9.2 [Arithmetic](#92-arithmetic)
   - 9.3 [Math Functions](#93-math-functions)
   - 9.4 [Rounding & Value Manipulation](#94-rounding--value-manipulation)
   - 9.5 [Clamping & Interpolation](#95-clamping--interpolation)
   - 9.6 [Geometric (2-D / 3-D)](#96-geometric-2-d--3-d)
   - 9.7 [Texture Sampling & Pixel Packing](#97-texture-sampling--pixel-packing)
10. [PglShaderBytecode.h — PSB Format](#10-pglshaderbytecodeh--psb-format)
    - 10.1 [Constants](#101-constants)
    - 10.2 [Operand Encoding](#102-operand-encoding)
    - 10.3 [Inline Literal Table](#103-inline-literal-table)
    - 10.4 [VM Opcodes](#104-vm-opcodes)
    - 10.5 [Register Map](#105-register-map)
    - 10.6 [Auto-Bound Uniforms](#106-auto-bound-uniforms)
    - 10.7 [Structs & Enums](#107-structs--enums)
    - 10.8 [Standalone Functions](#108-standalone-functions)
11. [PglShaderCompiler.h — PGLSL Compiler](#11-pglshadercompilerh--pglsl-compiler)
    - 11.1 [CompileResult](#111-compileresult)
    - 11.2 [Public API](#112-public-api)
    - 11.3 [Compiler Internals (private)](#113-compiler-internals-private)
12. [PglJobScheduler.h — Job Scheduler Interface](#12-pgjobschedulerh--job-scheduler-interface)
13. [PglJobScheduler_SingleCore.h — Serial Fallback](#13-pgljobscheduler_singlecoreh--serial-fallback)
14. [Quick Cross-Reference](#14-quick-cross-reference)

---

## 1. Overview

ProtoGL is a lightweight, Vulkan-inspired graphics command library that records GPU
draw calls and configuration into a binary command buffer on the **ESP32-S3 host** and
transmits frames over **Octal SPI** (high-bandwidth data plane) to an external **RP2350**
GPU co-processor. A separate **I2C** management bus handles low-frequency device
identification, configuration, status monitoring, and diagnostics — similar to SMBus
on PC platforms.

The library is split into:

| Component | Side | Purpose |
|---|---|---|
| `PglEncoder` | Host (ESP32-S3) | Record draw commands into a frame buffer |
| `PglDevice` | Host (ESP32-S3) | Manage transport (SPI + I2C), double-buffered DMA |
| `PglParser` | GPU (RP2350) | Alignment-safe deserialization of the command stream |
| `PglShaderBackend` | GPU (RP2350) | Platform-portable math primitives for shader execution |
| `PglShaderBytecode` | Shared | Binary shader format definitions (PSB) |
| `PglShaderCompiler` | Host (ESP32-S3) | PGLSL → PSB bytecode compiler |
| `PglJobScheduler` | GPU (RP2350) | Abstract parallel job dispatch |
| `PglTypes` / `PglOpcodes` | Shared | Wire types, enums, opcode constants |
| `PglCRC16` | Shared | Frame integrity check |

---

## 2. ProtoGL.h — Umbrella Header

```cpp
#include "ProtoGL.h"
```

Re-exports every public header in the correct order:

```
PglTypes.h → PglOpcodes.h → PglCRC16.h → PglEncoder.h →
PglDevice.h → PglParser.h → PglShaderBackend.h →
PglShaderBytecode.h → PglShaderCompiler.h →
PglJobScheduler.h → PglJobScheduler_SingleCore.h
```

Include **only** `ProtoGL.h` in application code.

---

## 3. PglTypes.h — Types, Enumerations & Constants

### 3.1 Resource Handle Types

All handles are lightweight integer typedefs. They identify GPU-side resources.

| Typedef | Underlying | Description |
|---|---|---|
| `PglCamera` | `uint8_t` | Camera identifier (max 4) |
| `PglMesh` | `uint16_t` | Mesh resource handle |
| `PglMaterial` | `uint16_t` | Material resource handle |
| `PglTexture` | `uint16_t` | Texture resource handle |
| `PglLayout` | `uint8_t` | Pixel-layout identifier |
| `PglMemHandle` | `uint16_t` | Opaque memory allocation handle |

### 3.2 Dimension & Limit Constants

| Constant | Value | Description |
|---|---|---|
| `PGL_MAX_CAMERAS` | `4` | Maximum simultaneous cameras |
| `PGL_MAX_DRAW_CALLS` | `64` | Maximum draw calls per frame |
| `PGL_MAX_SHADER_SLOTS` | `8` | Post-process shader slots per camera |
| `PGL_MAX_MESHES` | `32` | Maximum mesh resources |
| `PGL_MAX_MATERIALS` | `32` | Maximum material resources |
| `PGL_MAX_TEXTURES` | `64` | Maximum texture resources |
| `PGL_MAX_IMAGE_SEQUENCES` | `32` | Maximum image sequence resources |
| `PGL_MAX_FONTS` | `16` | Maximum custom font resources (plus 2 built-in) |
| `PGL_MAX_LAYOUTS` | `4` | Maximum pixel layouts |
| `PGL_MAX_VERTICES` | `4096` | Max vertices per mesh |
| `PGL_MAX_TRIANGLES` | `8192` | Max triangles per mesh |
| `PGL_MAX_SHADER_PROGRAMS` | `16` | Maximum shader program resources |

### 3.3 Wire Protocol Constants

| Constant | Value | Description |
|---|---|---|
| `PGL_SYNC_WORD` | `0x55AA` | Frame sync marker (little-endian) |
| `PGL_PROTOCOL_VERSION` | `0x07` | Current protocol version |
| `PGL_FRAME_HEADER_SIZE` | `12` | Frame header size in bytes |
| `PGL_I2C_DEFAULT_ADDR` | `0x3C` | Default GPU I2C slave address |

### 3.4 Enumerations

#### `PglMaterialType : uint8_t`

| Value | Name | Description |
|---|---|---|
| 0x00 | `PGL_MAT_SIMPLE` | Solid colour (RGB) — 3 bytes |
| 0x01 | `PGL_MAT_NORMAL` | Surface normal → colour mapping |
| 0x02 | `PGL_MAT_DEPTH` | Z-depth with near/far colours — 14 bytes |
| 0x10 | `PGL_MAT_GRADIENT` | Position-based gradient with stop array |
| 0x20 | `PGL_MAT_LIGHT` | Diffuse + ambient directional lighting — 18 bytes |
| 0x30 | `PGL_MAT_SIMPLEX_NOISE` | Simplex noise with two-colour palette — 22 bytes |
| 0x31 | `PGL_MAT_RAINBOW_NOISE` | Simplex noise RGB — 8 bytes |
| 0x40 | `PGL_MAT_IMAGE` | Texture-mapped (references a PglTexture) — 12 bytes |
| 0x41 | `PGL_MAT_IMAGE_SEQUENCE` | Animated texture sequence (references a PglImageSequence) — 13 bytes. GPU auto-advances frame. |
| 0x50 | `PGL_MAT_COMBINE` | Two-material blend (12 blend modes) — 9 bytes |
| 0x51 | `PGL_MAT_MASK` | Threshold-based compositing — 8 bytes |
| 0x52 | `PGL_MAT_ANIMATOR` | Time-based material interpolation — 9 bytes |
| 0xF0 | `PGL_MAT_PRERENDERED` | Opaque fallback (host pre-rendered texture) |

> **Memory placement:** All material parameter blocks (0x00–0x52) are 3–50 bytes and
> reside **permanently in SRAM** — they are read at per-pixel frequency. This includes
> `PGL_MAT_ANIMATOR` (which interpolates between two colour-based materials each frame).
> `PGL_MAT_IMAGE` and `PGL_MAT_IMAGE_SEQUENCE` store only a small param block in SRAM;
> the backing pixel data lives in the referenced Texture / ImageSequence resource, which
> the tiering manager places according to size (small textures in SRAM, large textures
> and all image-sequence atlases in external VRAM).

#### `PglShaderType : uint8_t`

| Value | Name | Description |
|---|---|---|
| 0 | `PGL_SHADER_NONE` | Disabled slot |
| 1 | `PGL_SHADER_CONVOLUTION` | Kernel convolution |
| 2 | `PGL_SHADER_DISPLACEMENT` | UV displacement |
| 3 | `PGL_SHADER_COLOR_ADJUST` | Colour adjustment |
| 4 | `PGL_SHADER_CUSTOM` | Programmable shader program |

#### `PglKernelShape : uint8_t`

| Value | Name | Description |
|---|---|---|
| 0 | `PGL_KERNEL_BOX` | Box blur kernel |
| 1 | `PGL_KERNEL_GAUSSIAN` | Gaussian kernel |
| 2 | `PGL_KERNEL_TRIANGLE` | Triangle (tent) kernel |

#### `PglDisplacementAxis : uint8_t`

| Value | Name | Description |
|---|---|---|
| 0 | `PGL_DISP_X` | Horizontal only |
| 1 | `PGL_DISP_Y` | Vertical only |
| 2 | `PGL_DISP_XY` | Both axes |

#### `PglWaveform : uint8_t`

| Value | Name | Description |
|---|---|---|
| 0 | `PGL_WAVE_SINE` | Sine wave |
| 1 | `PGL_WAVE_TRIANGLE` | Triangle wave |
| 2 | `PGL_WAVE_SQUARE` | Square wave |
| 3 | `PGL_WAVE_SAWTOOTH` | Sawtooth wave |

#### `PglColorAdjustOp : uint8_t`

| Value | Name | Description |
|---|---|---|
| 0 | `PGL_CADJ_EDGE_FEATHER` | Dim edges adjacent to black pixels |
| 1 | `PGL_CADJ_BRIGHTNESS` | Brightness offset |
| 2 | `PGL_CADJ_CONTRAST` | Contrast multiplier |
| 3 | `PGL_CADJ_GAMMA` | Gamma exponent |

#### `PglBlendMode : uint8_t`

| Value | Name | Description |
|---|---|---|
| 0 | `PGL_BLEND_REPLACE` | Overwrite destination |
| 1 | `PGL_BLEND_ALPHA` | Alpha blend |
| 2 | `PGL_BLEND_ADD` | Additive blend |
| 3 | `PGL_BLEND_MULTIPLY` | Multiplicative blend |
| 4 | `PGL_BLEND_SCREEN` | Screen blend |

#### `PglTextureFormat : uint8_t`

| Value | Name | Description |
|---|---|---|
| 0 | `PGL_TEX_RGB565` | 16-bit RGB 5-6-5 |
| 1 | `PGL_TEX_RGB888` | 24-bit RGB 8-8-8 |
| 2 | `PGL_TEX_RGBA8888` | 32-bit RGBA |
| 3 | `PGL_TEX_GRAYSCALE8` | 8-bit greyscale |

#### `PglGpuArch : uint8_t`

| Value | Name | Description |
|---|---|---|
| 0 | `PGL_ARCH_UNKNOWN` | Unknown architecture |
| 1 | `PGL_ARCH_RP2350_CM33` | RP2350 with Cortex-M33 cores |
| 2 | `PGL_ARCH_ESP32_P4` | ESP32-P4 (HP + LP core) |

#### `PglMemTier : uint8_t`

| Value | Name | Description |
|---|---|---|
| 0 | `PGL_MEM_SRAM` | On-chip SRAM (520 KB on RP2350) |
| 1 | `PGL_MEM_QSPI_A` | QSPI-A VRAM (up to 2 chips, RP2350B) |
| 2 | `PGL_MEM_QSPI_B` | QSPI-B VRAM (up to 2 chips, RP2350B) |

#### `PglMemResourceClass : uint8_t`

| Value | Name | Description |
|---|---|---|
| 0 | `PGL_RES_MESH` | Mesh vertex/index data |
| 1 | `PGL_RES_TEXTURE` | Texture pixel data |
| 2 | `PGL_RES_MATERIAL` | Material parameters (always SRAM) |
| 3 | `PGL_RES_FRAMEBUFFER` | Framebuffer (pinned SRAM, read-only class) |
| 4 | `PGL_RES_IMAGE_SEQUENCE` | Image sequence atlas (external VRAM default) |
| 5 | `PGL_RES_FONT_ATLAS` | Custom font glyph atlas (external VRAM default) |
| 6 | `PGL_RES_VERTEX_DATA` | Vertex position/normal data |
| 7 | `PGL_RES_INDEX_DATA` | Triangle index data |
| 8 | `PGL_RES_UV_DATA` | Texture coordinate data |
| 9 | `PGL_RES_LAYOUT_COORDS` | Pixel layout (LED position) coordinates |
| 10 | `PGL_RES_LOOKUP_TABLE` | Lookup table (gamma, trig, etc.) |
| 11 | `PGL_RES_SHADER_PROGRAM` | Compiled shader bytecode |
| 12 | `PGL_RES_Z_BUFFER` | Z-buffer (pinned SRAM, read-only class) |
| 13 | `PGL_RES_QUADTREE` | QuadTree nodes (pinned SRAM, read-only class) |

#### `PglI2CRegister : uint8_t`

| Value | Name | Description |
|---|---|---|
| `0x01` | `PGL_REG_BRIGHTNESS` | Display brightness (1 byte) |
| `0x02` | `PGL_REG_PANEL_WIDTH` | Panel width (2 bytes LE) |
| `0x03` | `PGL_REG_PANEL_HEIGHT` | Panel height (2 bytes LE) |
| `0x04` | `PGL_REG_SCAN_RATE` | Scan rate (1 byte) |
| `0x05` | `PGL_REG_CLEAR` | Clear display (write 0x01) |
| `0x06` | `PGL_REG_GAMMA_TABLE` | Gamma table index (1 byte) |
| `0x07` | `PGL_REG_RESET` | Reset GPU (write 0x52) |
| `0x08` | `PGL_REG_QUERY_STATUS` | Query status → 8 bytes |
| `0x09` | `PGL_REG_QUERY_CAPS` | Query capabilities → 16 bytes |
| `0x0A` | `PGL_REG_QUERY_EXT_STATUS` | Query extended status → 32 bytes |
| `0x0B` | `PGL_REG_SET_CLOCK` | Set clock frequency (4 bytes) |

#### Status / Capability Bit Flags

**`PglStatusFlags : uint8_t`**

| Bit | Name | Description |
|---|---|---|
| 0 | `PGL_STATUS_READY` | GPU ready for new frame |
| 1 | `PGL_STATUS_RENDERING` | Currently rendering |
| 2 | `PGL_STATUS_ERROR` | Error condition active |

**`PglVramFlags : uint8_t`**

| Bit | Name | Description |
|---|---|---|
| 0 | `PGL_VRAM_HAS_QSPI_A` | QSPI-A channel detected |
| 1 | `PGL_VRAM_HAS_QSPI_B` | QSPI-B channel detected |

**`PglClockFlags : uint8_t`**

| Bit | Name | Description |
|---|---|---|
| 0 | `PGL_CLOCK_RECONFIGURE_PIO` | Reconfigure PIO clocks after frequency change |

**`PglCapabilityFlags : uint8_t`**

| Bit | Name | Description |
|---|---|---|
| 0 | `PGL_CAP_DUAL_CORE` | Dual-core rendering available |
| 1 | `PGL_CAP_PROGRAMMABLE_SHADERS` | Programmable shader support |
| 2 | `PGL_CAP_EXTERNAL_VRAM` | External QSPI VRAM (RP2350B) |

**`PglAllocStatusFlags : uint8_t`**

| Bit | Name | Description |
|---|---|---|
| 0 | `PGL_ALLOC_OK` | Allocation succeeded |
| 1 | `PGL_ALLOC_OOM` | Out of memory |
| 2 | `PGL_ALLOC_INVALID_TIER` | Invalid memory tier |

### 3.5 Wire-Format Structures

All structures are `__attribute__((packed))` and little-endian.

#### `PglVec2`

| Field | Type | Description |
|---|---|---|
| `x` | `float` | X component |
| `y` | `float` | Y component |

#### `PglVec3`

| Field | Type | Description |
|---|---|---|
| `x` | `float` | X component |
| `y` | `float` | Y component |
| `z` | `float` | Z component |

#### `PglIndex3`

| Field | Type | Description |
|---|---|---|
| `a` | `uint16_t` | First vertex index |
| `b` | `uint16_t` | Second vertex index |
| `c` | `uint16_t` | Third vertex index |

#### `PglVertexDelta`

| Field | Type | Description |
|---|---|---|
| `index` | `uint16_t` | Vertex index to update |
| `position` | `PglVec3` | New position |

#### `PglFrameHeader`

| Field | Type | Description |
|---|---|---|
| `sync` | `uint16_t` | `PGL_SYNC_WORD` (0x55AA) |
| `version` | `uint8_t` | `PGL_PROTOCOL_VERSION` |
| `flags` | `uint8_t` | Reserved |
| `frameNumber` | `uint32_t` | Monotonic frame counter |
| `commandCount` | `uint16_t` | Number of commands in frame |
| `payloadLength` | `uint16_t` | Total payload bytes (excl. header) |

#### `PglCommandHeader`

| Field | Type | Description |
|---|---|---|
| `opcode` | `uint8_t` | Command opcode |
| `length` | `uint16_t` | Payload length in bytes |

#### `PglShaderParams`

| Field | Type | Description |
|---|---|---|
| `type` | `PglShaderType` | Shader type |
| `intensity` | `float` | Effect intensity [0–1] |
| *union* | 12 bytes | Type-specific parameters |

**Convolution sub-fields:**
`PglKernelShape kernel`, `uint8_t radius`, `bool separable`, `float angleDeg`, `float anglePeriod`, `float sigma`

**Displacement sub-fields:**
`PglDisplacementAxis axis`, `bool perChannel`, `uint8_t amplitude`, `PglWaveform waveform`, `float period`, `float frequency`, `float phase1Period`, `float phase2Period`

**Color Adjust sub-fields:**
`PglColorAdjustOp operation`, `float strength`, `float param2`

**Custom (Programmable) sub-fields:**
`uint16_t programId`

#### `PglMaterialParams`

| Field | Type | Description |
|---|---|---|
| `type` | `PglMaterialType` | Material type |
| `blendMode` | `PglBlendMode` | Blend mode |
| `dataSize` | `uint16_t` | Size of following type-specific data |

### 3.6 I2C Response Structures

#### `PglStatusResponse` (8 bytes)

| Field | Type | Description |
|---|---|---|
| `flags` | `PglStatusFlags` | Ready/rendering/error bits |
| `currentFrame` | `uint32_t` | Last completed frame number |
| `queueDepth` | `uint8_t` | Frames queued |
| `errorCode` | `uint8_t` | Last error code |
| `reserved` | `uint8_t` | — |

#### `PglCapabilityResponse` (16 bytes)

| Field | Type | Description |
|---|---|---|
| `arch` | `PglGpuArch` | GPU architecture |
| `coreCount` | `uint8_t` | Active core count |
| `clockMHz` | `uint16_t` | Current clock frequency |
| `sramKB` | `uint16_t` | SRAM kilobytes |
| `psramKB` | `uint16_t` | PSRAM kilobytes |
| `mramKB` | `uint16_t` | MRAM kilobytes |
| `capFlags` | `PglCapabilityFlags` | Feature flags |
| `maxMeshes` | `uint8_t` | Max mesh resources |
| `maxTextures` | `uint8_t` | Max texture resources |
| `maxShaderSlots` | `uint8_t` | Max shader slots per camera |
| `protocolVersion` | `uint8_t` | Supported protocol version |
| `reserved` | `uint8_t` | — |

#### `PglExtendedStatusResponse` (32 bytes)

| Field | Type | Description |
|---|---|---|
| `flags` | `PglStatusFlags` | Same as basic status |
| `currentFrame` | `uint32_t` | Last completed frame |
| `cpuUsagePercent` | `uint8_t` | Core 0 CPU % |
| `cpu1UsagePercent` | `uint8_t` | Core 1 CPU % |
| `temperatureC` | `int8_t` | Die temperature °C |
| `sramUsedKB` | `uint16_t` | SRAM used |
| `psramUsedKB` | `uint16_t` | PSRAM used |
| `mramUsedKB` | `uint16_t` | MRAM used |
| `lastFrameTimeUs` | `uint32_t` | Last frame render time µs |
| `avgFrameTimeUs` | `uint32_t` | Rolling average render time µs |
| `peakFrameTimeUs` | `uint32_t` | Peak frame render time µs |
| `droppedFrameCount` | `uint16_t` | GPU-side dropped frames |
| `vramFlags` | `PglVramFlags` | External VRAM present bits |
| `reserved` | `uint8_t[2]` | — |

#### `PglPersistStatusResponse` (12 bytes) *(v0.7.1)*

| Field | Type | Description |
|---|---|---|
| `status` | `uint8_t` | 0=none, 1=in-progress, 2=complete, 3=error |
| `resourceClass` | `uint8_t` | Queried resource class |
| `resourceId` | `uint16_t` | Queried resource ID |
| `flashAddr` | `uint32_t` | Flash address (0 if not persisted) |
| `sizeBytes` | `uint16_t` | Persisted data size in bytes |
| `reserved` | `uint16_t` | — |

When queried in manifest summary mode (`PGL_PERSIST_QUERY_MANIFEST`), fields are reinterpreted:

| Field | Reinterpretation |
|---|---|
| `status` | Always 0x02 (info) |
| `resourceClass` | `totalEntries` (max 64) |
| `resourceId` | `usedEntries` |
| `flashAddr` | `freeBytesLow32` |
| `sizeBytes` | Reserved |

I2C register: `MEM_PERSIST_STATUS` (0x1C). SPI read: `SPI_READ_PERSIST_STATUS` (0xEB).

---

## 4. PglOpcodes.h — Command Opcodes

Every command in the binary frame stream begins with a `PglCommandHeader` whose
`opcode` field is one of the constants below.

### Resource Management — Mesh (0x01–0x04)

| Constant | Value | Description |
|---|---|---|
| `PGL_OP_CREATE_MESH` | `0x01` | Upload mesh geometry |
| `PGL_OP_DESTROY_MESH` | `0x02` | Destroy mesh |
| `PGL_OP_UPDATE_VERTICES` | `0x03` | Full vertex buffer replace |
| `PGL_OP_UPDATE_VERTICES_DELTA` | `0x04` | Sparse vertex delta |

### Resource Management — Material (0x10–0x12)

| Constant | Value | Description |
|---|---|---|
| `PGL_OP_CREATE_MATERIAL` | `0x10` | Create material |
| `PGL_OP_UPDATE_MATERIAL` | `0x11` | Update material params |
| `PGL_OP_DESTROY_MATERIAL` | `0x12` | Destroy material |

### Resource Management — Texture, Image Sequence, Font (0x18–0x1E)

| Constant | Value | Description |
|---|---|---|
| `PGL_OP_CREATE_TEXTURE` | `0x18` | Upload texture (RGB565, RGB888, RGBA8888, GRAYSCALE8) |
| `PGL_OP_DESTROY_TEXTURE` | `0x19` | Destroy texture |
| `PGL_OP_UPDATE_TEXTURE` | `0x1A` | Partial or full texture pixel data replacement |
| `PGL_OP_CREATE_IMAGE_SEQUENCE` | `0x1B` | Upload multi-frame animated texture atlas |
| `PGL_OP_DESTROY_IMAGE_SEQUENCE` | `0x1C` | Destroy image sequence |
| `PGL_OP_CREATE_FONT` | `0x1D` | Upload custom glyph atlas with metrics |
| `PGL_OP_DESTROY_FONT` | `0x1E` | Destroy font |

### Layout & Camera (0x20–0x22)

| Constant | Value | Description |
|---|---|---|
| `PGL_OP_SET_PIXEL_LAYOUT` | `0x20` | Set pixel layout (irregular or rectangular) |
| `PGL_OP_SET_CAMERA` | `0x22` | Configure camera |

### Memory Access (0x30–0x44)

| Constant | Value | Description |
|---|---|---|
| `PGL_OP_MEM_WRITE` | `0x30` | Write raw bytes to GPU memory |
| `PGL_OP_MEM_READ_REQ` | `0x31` | Stage GPU memory for SPI readback |
| `PGL_OP_SET_RESOURCE_TIER` | `0x32` | Set preferred memory tier |
| `PGL_OP_MEM_ALLOC` | `0x33` | Allocate GPU memory region |
| `PGL_OP_MEM_FREE` | `0x34` | Free GPU memory allocation |
| `PGL_OP_FRAMEBUFFER_CAPTURE` | `0x35` | Capture framebuffer snapshot |
| `PGL_OP_MEM_COPY` | `0x36` | GPU-internal cross-tier memory copy |
| `PGL_OP_MEM_CREATE_POOL` | `0x38` | Create a named memory pool *(v0.7)* |
| `PGL_OP_MEM_DESTROY_POOL` | `0x39` | Destroy a memory pool *(v0.7)* |
| `PGL_OP_MEM_POOL_ALLOC` | `0x3A` | Allocate from a pool *(v0.7)* |
| `PGL_OP_MEM_POOL_FREE` | `0x3B` | Free a pool allocation *(v0.7)* |
| `PGL_OP_MEM_DEFRAG` | `0x3C` | Trigger pool defragmentation *(v0.7)* |
| `PGL_OP_STREAM_OPEN` | `0x3D` | Open a streaming upload channel *(v0.7)* |
| `PGL_OP_STREAM_DATA` | `0x3E` | Append data to stream *(v0.7)* |
| `PGL_OP_STREAM_CLOSE` | `0x3F` | Close and commit stream *(v0.7)* |
| `PGL_OP_BIND_RESOURCE` | `0x40` | Bind resource to slot *(v0.7)* |
| `PGL_OP_UNBIND_RESOURCE` | `0x41` | Unbind resource from slot *(v0.7)* |
| `PGL_OP_SMW_WRITE` | `0x42` | Write to Host→GPU shared mailbox slot *(v0.7)* |
| `PGL_OP_SMW_READ` | `0x43` | Read GPU→Host shared mailbox *(v0.7)* |
| `PGL_OP_STAGE_READ` | `0x44` | Stage GPU memory into SMW bulk buffer *(v0.7)* |

### Persistence & Direct Framebuffer (0x45–0x48) *(v0.7.1)*

| Constant | Value | Description |
|---|---|---|
| `PGL_OP_WRITE_FRAMEBUFFER` | `0x45` | Write raw pixel data to output FB or compositor layer |
| `PGL_OP_PERSIST_RESOURCE` | `0x46` | Persist resource to flash (PSRAM) or mark persistent (MRAM) |
| `PGL_OP_RESTORE_RESOURCE` | `0x47` | Restore persisted resource from flash/MRAM after reboot |
| `PGL_OP_QUERY_PERSISTENCE` | `0x48` | Query persistence status (per-resource or manifest summary) |

### Rendering (0x80–0x8F)

| Constant | Value | Description |
|---|---|---|
| `PGL_OP_BEGIN_FRAME` | `0x80` | Start new frame (frame number + timestamp) |
| `PGL_OP_DRAW_OBJECT` | `0x81` | Draw mesh with material + transform |
| `PGL_OP_SET_CAMERA` | `0x82` | Set camera position/rotation/projection |
| `PGL_OP_SET_SHADER` | `0x83` | Set built-in post-process shader on camera slot |
| `PGL_OP_END_FRAME` | `0x8F` | End frame (triggers rasterization) |

### Programmable Shaders (0x84–0x87)

| Constant | Value | Description |
|---|---|---|
| `PGL_OP_CREATE_SHADER_PROGRAM` | `0x84` | Upload compiled PSB bytecode |
| `PGL_OP_DESTROY_SHADER_PROGRAM` | `0x85` | Destroy shader program |
| `PGL_OP_BIND_SHADER_PROGRAM` | `0x86` | Bind program to camera shader slot |
| `PGL_OP_SET_SHADER_UNIFORM` | `0x87` | Set shader uniform value |

### Display Commands (0x90–0x92) *(v0.7)*

| Constant | Value | Description |
|---|---|---|
| `PGL_OP_SET_DISPLAY_MODE` | `0x90` | Configure display driver |
| `PGL_OP_SET_DISPLAY_TIMING` | `0x91` | Set display timing parameters |
| `PGL_OP_SET_DISPLAY_ROUTING` | `0x92` | Route camera → display output |

### 2D Graphics / Layer Commands (0xA0–0xB0) *(v0.7)*

| Constant | Value | Description |
|---|---|---|
| `PGL_OP_LAYER_CREATE` | `0xA0` | Create compositing layer |
| `PGL_OP_LAYER_DESTROY` | `0xA1` | Destroy layer |
| `PGL_OP_LAYER_SET_PROPS` | `0xA2` | Set layer properties (pos, size, blend, opacity) |
| `PGL_OP_LAYER_SET_ORDER` | `0xA3` | Set layer Z-order |
| `PGL_OP_LAYER_CLEAR` | `0xA4` | Clear layer to solid colour |
| `PGL_OP_DRAW_RECT` | `0xA5` | Draw filled/stroked rectangle |
| `PGL_OP_DRAW_CIRCLE` | `0xA6` | Draw filled/stroked circle |
| `PGL_OP_DRAW_LINE` | `0xA7` | Draw anti-aliased line |
| `PGL_OP_DRAW_TRIANGLE_2D` | `0xA8` | Draw filled triangle |
| `PGL_OP_DRAW_ARC` | `0xA9` | Draw arc/pie sector |
| `PGL_OP_DRAW_ROUNDED_RECT` | `0xAA` | Draw rounded rectangle |
| `PGL_OP_DRAW_POLYLINE` | `0xAB` | Draw connected polyline |
| `PGL_OP_DRAW_SPRITE` | `0xAC` | Blit texture sprite |
| `PGL_OP_DRAW_TEXT` | `0xAD` | Render text string |
| `PGL_OP_COMPOSITE_LAYERS` | `0xAE` | Trigger compositing pipeline |
| `PGL_OP_DRAW_BILLBOARD` | `0xAF` | Billboard sprite in 3D world space *(v0.7)* |
| `PGL_OP_SET_LAYER_SHADER` | `0xB0` | Bind PSB shader to 2D layer *(v0.7)* |

---

## 5. PglCRC16.h — CRC-16/XMODEM

Frame integrity is verified with CRC-16/XMODEM (polynomial 0x1021).

### Class: `PglCRC16`

```cpp
class PglCRC16 {
public:
    static uint16_t Compute(const uint8_t* data, size_t length);
    static uint16_t Update(uint16_t crc, const uint8_t* data, size_t length);
};
```

| Method | Description |
|---|---|
| `Compute(data, length)` | Compute CRC-16 over a complete buffer. Equivalent to `Update(0xFFFF, data, length)`. |
| `Update(crc, data, length)` | Continue an incremental CRC calculation. Pass previous CRC as the first argument. |

**Usage:**

```cpp
uint16_t crc = PglCRC16::Compute(frameBytes, frameLength);
// Or incrementally:
uint16_t crc = 0xFFFF;
crc = PglCRC16::Update(crc, header, headerLen);
crc = PglCRC16::Update(crc, payload, payloadLen);
```

---

## 6. PglEncoder.h — Command Buffer Encoder

`PglEncoder` serialises GPU commands into a pre-allocated byte buffer. It is the
primary host-side API — every draw call, resource upload, shader configuration, and
memory operation passes through the encoder.

### 6.1 Construction & State

```cpp
PglEncoder(uint8_t* buffer, size_t capacity);
```

| Parameter | Type | Description |
|---|---|---|
| `buffer` | `uint8_t*` | Caller-owned buffer for command recording |
| `capacity` | `size_t` | Buffer size in bytes |

**State queries:**

| Method | Signature | Description |
|---|---|---|
| `GetWritePosition` | `size_t GetWritePosition() const` | Current write offset in buffer |
| `GetCommandCount` | `uint16_t GetCommandCount() const` | Commands recorded so far in current frame |
| `HasOverflow` | `bool HasOverflow() const` | `true` if any write exceeded capacity |

> **Note:** After an overflow the frame is corrupt. Check `HasOverflow()` before
> calling `EndFrame()`.

### 6.2 Frame Lifecycle

| Method | Signature | Description |
|---|---|---|
| `BeginFrame` | `void BeginFrame(uint32_t frameNumber, uint32_t frameTimeUs)` | Write frame header, reset counters |
| `EndFrame` | `void EndFrame()` | Patch payload length + CRC-16, finalise frame |

**Usage pattern:**

```cpp
encoder->BeginFrame(frameNum, micros());
// ... issue commands ...
encoder->EndFrame();
```

### 6.3 Camera Commands

```cpp
void SetCamera(PglCamera cameraId,
               const PglVec3& position,
               const PglVec3& target,
               const PglVec3& up,
               float fov,
               float nearPlane,
               float farPlane);
```

| Parameter | Type | Description |
|---|---|---|
| `cameraId` | `PglCamera` | Camera index (0–3) |
| `position` | `PglVec3` | Eye position |
| `target` | `PglVec3` | Look-at point |
| `up` | `PglVec3` | Up vector |
| `fov` | `float` | Vertical field of view in degrees |
| `nearPlane` | `float` | Near clip plane distance |
| `farPlane` | `float` | Far clip plane distance |

Emits `PGL_OP_SET_CAMERA`.

### 6.4 Draw Commands

```cpp
void DrawObject(PglCamera cameraId,
                PglMesh meshId,
                PglMaterial materialId,
                const PglVec3& position,
                const PglVec3& rotation,
                const PglVec3& scale);
```

| Parameter | Type | Description |
|---|---|---|
| `cameraId` | `PglCamera` | Camera to render into |
| `meshId` | `PglMesh` | Mesh resource handle |
| `materialId` | `PglMaterial` | Material resource handle |
| `position` | `PglVec3` | World-space translation |
| `rotation` | `PglVec3` | Euler rotation (degrees) |
| `scale` | `PglVec3` | Per-axis scale factors |

Emits `PGL_OP_DRAW_OBJECT`. Max `PGL_MAX_DRAW_CALLS` (64) per frame.

### 6.5 Built-in Shaders — General

```cpp
void SetShader(PglCamera cameraId, uint8_t slot, const PglShaderParams& params);
void ClearShader(PglCamera cameraId, uint8_t slot);
```

| Method | Description |
|---|---|
| `SetShader` | Assign a fully-populated `PglShaderParams` to a camera's shader slot (0–7). |
| `ClearShader` | Clear a shader slot, disabling the post-process effect. |

### 6.6 Built-in Shaders — Convolution

| Method | Signature |
|---|---|
| `SetConvolution` | `void SetConvolution(PglCamera cameraId, uint8_t slot, float intensity, PglKernelShape kernel, uint8_t radius, bool separable, float angleDeg, float anglePeriod = 0.0f, float sigma = 0.0f)` |
| `SetHorizontalBlur` | `void SetHorizontalBlur(PglCamera cameraId, uint8_t slot, float intensity, uint8_t radius)` |
| `SetVerticalBlur` | `void SetVerticalBlur(PglCamera cameraId, uint8_t slot, float intensity, uint8_t radius)` |
| `SetRadialBlur` | `void SetRadialBlur(PglCamera cameraId, uint8_t slot, float intensity, uint8_t radius, float rotationPeriod = 3.7f)` |
| `SetAntiAliasing` | `void SetAntiAliasing(PglCamera cameraId, uint8_t slot, float intensity, float smoothing = 0.25f)` |

**Parameters (common):**

| Parameter | Type | Description |
|---|---|---|
| `cameraId` | `PglCamera` | Camera owning the shader slot |
| `slot` | `uint8_t` | Shader slot index (0–7) |
| `intensity` | `float` | Effect intensity [0.0 – 1.0] |
| `radius` | `uint8_t` | Kernel radius in pixels |

**`SetConvolution` additional parameters:**

| Parameter | Type | Default | Description |
|---|---|---|---|
| `kernel` | `PglKernelShape` | — | Kernel shape (box/gaussian/triangle) |
| `separable` | `bool` | — | Enable 2-pass separable optimisation |
| `angleDeg` | `float` | — | Blur direction in degrees |
| `anglePeriod` | `float` | `0.0` | Rotation animation period (0 = static) |
| `sigma` | `float` | `0.0` | Gaussian sigma (0 = auto from radius) |

**Convenience wrappers:**

- `SetHorizontalBlur` — box kernel, angle = 0°
- `SetVerticalBlur` — box kernel, angle = 90°
- `SetRadialBlur` — box kernel, auto-rotating at `rotationPeriod` seconds
- `SetAntiAliasing` — separable 4-neighbour smoothing, 2D Gaussian

### 6.7 Built-in Shaders — Displacement

| Method | Signature |
|---|---|
| `SetDisplacement` | `void SetDisplacement(PglCamera cameraId, uint8_t slot, float intensity, PglDisplacementAxis axis, bool perChannel, uint8_t amplitude, PglWaveform waveform, float period, float frequency = 1.0f, float phase1Period = 0.0f, float phase2Period = 0.0f)` |
| `SetPhaseOffsetX` | `void SetPhaseOffsetX(PglCamera cameraId, uint8_t slot, float intensity, uint8_t amplitude, float period = 3.5f)` |
| `SetPhaseOffsetY` | `void SetPhaseOffsetY(PglCamera cameraId, uint8_t slot, float intensity, uint8_t amplitude, float period = 3.5f)` |
| `SetPhaseOffsetR` | `void SetPhaseOffsetR(PglCamera cameraId, uint8_t slot, float intensity, uint8_t amplitude, float rotPeriod = 3.7f, float phase1Period = 4.5f, float phase2Period = 3.2f)` |

**`SetDisplacement` parameters:**

| Parameter | Type | Default | Description |
|---|---|---|---|
| `axis` | `PglDisplacementAxis` | — | Displacement axis (X, Y, or XY) |
| `perChannel` | `bool` | — | Per-channel (chromatic aberration) mode |
| `amplitude` | `uint8_t` | — | Displacement amplitude in pixels |
| `waveform` | `PglWaveform` | — | Wave shape |
| `period` | `float` | — | Animation period in seconds |
| `frequency` | `float` | `1.0` | Spatial frequency multiplier |
| `phase1Period` | `float` | `0.0` | Secondary phase animation period |
| `phase2Period` | `float` | `0.0` | Tertiary phase animation period |

**Convenience wrappers:**

- `SetPhaseOffsetX` — horizontal chromatic aberration (X-axis, per-channel, sine wave)
- `SetPhaseOffsetY` — vertical chromatic aberration
- `SetPhaseOffsetR` — radial chromatic aberration with independent phase animation

### 6.8 Built-in Shaders — Color Adjust

| Method | Signature |
|---|---|
| `SetColorAdjust` | `void SetColorAdjust(PglCamera cameraId, uint8_t slot, float intensity, PglColorAdjustOp operation, float strength, float param2 = 0.0f)` |
| `SetEdgeFeather` | `void SetEdgeFeather(PglCamera cameraId, uint8_t slot, float intensity, float featherStrength = 0.5f)` |
| `SetBrightness` | `void SetBrightness(PglCamera cameraId, uint8_t slot, float intensity, float brightness)` |
| `SetContrast` | `void SetContrast(PglCamera cameraId, uint8_t slot, float intensity, float contrast)` |
| `SetGamma` | `void SetGamma(PglCamera cameraId, uint8_t slot, float intensity, float gammaExponent = 2.2f)` |

**`SetColorAdjust` parameters:**

| Parameter | Type | Default | Description |
|---|---|---|---|
| `operation` | `PglColorAdjustOp` | — | Operation type |
| `strength` | `float` | — | Effect strength (meaning is op-dependent) |
| `param2` | `float` | `0.0` | Secondary parameter (e.g. gamma exponent) |

**Convenience wrappers:**

| Method | Operation | Notes |
|---|---|---|
| `SetEdgeFeather` | `PGL_CADJ_EDGE_FEATHER` | Dims pixels adjacent to black; `featherStrength` defaults to 0.5 |
| `SetBrightness` | `PGL_CADJ_BRIGHTNESS` | Additive brightness offset |
| `SetContrast` | `PGL_CADJ_CONTRAST` | Multiplicative contrast |
| `SetGamma` | `PGL_CADJ_GAMMA` | Power-curve correction, default exponent 2.2 |

### 6.9 Programmable Shaders (v0.6)

Programmable shaders allow uploading custom **PSB bytecode** (compiled from PGLSL by
`PglShaderCompiler`) to be executed per-pixel on the GPU.

```cpp
void CreateShaderProgram(uint16_t programId,
                         const void* bytecodeBlob,
                         uint16_t bytecodeSize);

void DestroyShaderProgram(uint16_t programId);

void BindShaderProgram(PglCamera cameraId,
                       uint8_t shaderSlot,
                       uint16_t programId,
                       float intensity);
```

| Method | Description |
|---|---|
| `CreateShaderProgram` | Upload compiled PSB bytecode for `programId`. Max `PSB_MAX_PROGRAM_SIZE` (1296) bytes. |
| `DestroyShaderProgram` | Free GPU resources for the given program. |
| `BindShaderProgram` | Bind program to a camera shader slot with configurable `intensity` [0–1]. |

**Uniform setters (overloaded):**

```cpp
void SetShaderUniform(uint16_t programId, uint8_t uniformSlot, float value);
void SetShaderUniform(uint16_t programId, uint8_t uniformSlot, float x, float y);
void SetShaderUniform(uint16_t programId, uint8_t uniformSlot, float x, float y, float z);
void SetShaderUniform(uint16_t programId, uint8_t uniformSlot, float x, float y, float z, float w);
```

| Parameter | Type | Description |
|---|---|---|
| `programId` | `uint16_t` | Target shader program |
| `uniformSlot` | `uint8_t` | Uniform slot (0–15). Slots 0–2 are auto-bound (resolution, time). |
| `value/x/y/z/w` | `float` | Uniform component values |

**Typical flow:**

```cpp
// 1. Compile PGLSL → PSB on the host
auto result = PglShaderCompiler::Compile(source, strlen(source));

// 2. Upload bytecode to GPU
encoder->CreateShaderProgram(1, result.bytecode, result.bytecodeSize);

// 3. Bind to camera 0, slot 0
encoder->BindShaderProgram(0, 0, 1, 1.0f);

// 4. Set custom uniforms
encoder->SetShaderUniform(1, 3, myValue);  // slot 3 = first user uniform
```

### 6.10 Mesh Resources

```cpp
void CreateMesh(PglMesh meshId,
                const PglVec3* vertices, uint16_t vertexCount,
                const PglIndex3* indices, uint16_t triangleCount,
                bool hasUV = false,
                const PglVec2* uvVertices = nullptr,
                uint16_t uvVertexCount = 0,
                const PglIndex3* uvIndices = nullptr);

void DestroyMesh(PglMesh meshId);

void UpdateVertices(PglMesh meshId,
                    const PglVec3* vertices,
                    uint16_t vertexCount);

void UpdateVerticesDelta(PglMesh meshId,
                         const PglVertexDelta* deltas,
                         uint16_t deltaCount);
```

| Method | Description |
|---|---|
| `CreateMesh` | Upload a new mesh. Optionally includes UV coordinates for textured materials. Max `PGL_MAX_VERTICES` vertices, `PGL_MAX_TRIANGLES` triangles. |
| `DestroyMesh` | Free GPU-side mesh resources. |
| `UpdateVertices` | Replace the entire vertex buffer. Vertex count must match the original. |
| `UpdateVerticesDelta` | Sparse update — only the listed vertices are patched; bandwidth-efficient for morphs/blend shapes. |

### 6.11 Material Resources

```cpp
void CreateMaterial(PglMaterial materialId,
                    PglMaterialType type,
                    PglBlendMode blendMode,
                    const void* params,
                    uint16_t paramSize);

void UpdateMaterial(PglMaterial materialId,
                    const void* params,
                    uint16_t paramSize);

void DestroyMaterial(PglMaterial materialId);
```

| Method | Description |
|---|---|
| `CreateMaterial` | Create a material of the given `type`. The `params` blob is type-specific (e.g. colour values for flat, gradient stops, texture ID). |
| `UpdateMaterial` | Update the parameters of an existing material without re-creating it. |
| `DestroyMaterial` | Free GPU-side material resources. |

### 6.12 Texture Resources

```cpp
void CreateTexture(PglTexture textureId,
                   uint16_t width, uint16_t height,
                   PglTextureFormat format,
                   const void* pixelData);

void UpdateTexture(PglTexture textureId,
                   uint16_t offsetX, uint16_t offsetY,
                   uint16_t regionW, uint16_t regionH,
                   const void* pixelData);

void DestroyTexture(PglTexture textureId);
```

| Method | Description |
|---|---|
| `CreateTexture` | Upload pixel data. Pixel data size is derived from `width × height × bpp(format)`. Small textures (≤ ~4 KB) remain in SRAM; larger ones are placed in external VRAM. |
| `UpdateTexture` | Partial or full pixel data replacement. If `offsetX=0, offsetY=0, regionW=width, regionH=height`, replaces all pixels. |
| `DestroyTexture` | Free GPU-side texture resources. |

**Bytes per pixel by format:**

| Format | BPP |
|---|---|
| `PGL_TEX_RGB565` | 2 |
| `PGL_TEX_RGB888` | 3 |
| `PGL_TEX_RGBA8888` | 4 |
| `PGL_TEX_GRAYSCALE8` | 1 |

### 6.12.1 Image Sequence Resources

An `ImageSequence` stores multiple frames in a single atlas for GPU-side animated textures. The GPU auto-advances the active frame based on `frameTimeUs` and the sequence's FPS. Both 3D materials (`PGL_MAT_IMAGE_SEQUENCE`) and 2D sprites (`PGL_SPRITE_SRC_SEQUENCE`) can reference an image sequence.

```cpp
void CreateImageSequence(PglImageSequence sequenceId,
                         uint16_t frameWidth, uint16_t frameHeight,
                         uint16_t frameCount,
                         PglTextureFormat format,
                         PglLoopMode loopMode,
                         float fps,
                         const void* pixelData);

void CreateImageSequenceHeader(PglImageSequence sequenceId,
                               uint16_t frameWidth, uint16_t frameHeight,
                               uint16_t frameCount,
                               PglTextureFormat format,
                               PglLoopMode loopMode,
                               float fps);

void DestroyImageSequence(PglImageSequence sequenceId);
```

| Method | Description |
|---|---|
| `CreateImageSequence` | Upload all frames in one command. Suitable for small sequences that fit in a single command buffer. |
| `CreateImageSequenceHeader` | Create the sequence metadata only (no pixel data). Use with `MemStreamBegin/Chunk/End` to upload large sequences across multiple frames. |
| `DestroyImageSequence` | Free GPU-side image sequence resources. |

**Memory:** Image sequence atlases are **always placed in external VRAM** (Tier 1/2). Only the active frame is DMA-fetched into the SRAM cache arena during rasterization.

**Limits:** `PGL_MAX_IMAGE_SEQUENCES` = 32.

### 6.12.2 Font Resources

Custom font atlases enable high-quality text rendering beyond the two built-in bitmap fonts. Once uploaded, fonts are shared resources usable by both 2D `CMD_DRAW_TEXT` and any future 3D text support.

```cpp
void CreateFont(PglFont fontId,
                uint16_t atlasWidth, uint16_t atlasHeight,
                PglTextureFormat format,
                uint8_t glyphCount, uint8_t firstChar,
                uint8_t lineHeight, uint8_t baseline,
                uint8_t flags,
                const PglGlyphMetrics* glyphs,
                const void* atlasPixelData);

void DestroyFont(PglFont fontId);
```

| Method | Description |
|---|---|
| `CreateFont` | Upload glyph atlas + per-character metrics. Format is typically `PGL_TEX_GRAYSCALE8` for anti-aliased fonts. |
| `DestroyFont` | Free GPU-side font resources. |

**Memory:** Font atlases are read-only after upload. Default tier: external VRAM (prefer MRAM channel). Small fonts (< 2 KB) may stay in SRAM.

**Limits:** `PGL_MAX_FONTS` = 16 (plus 2 built-in fonts at indices 0 and 1).

### 6.13 Pixel Layout

Pixel layouts describe the physical mapping from logical pixel indices to 2-D screen
coordinates. Required for non-standard LED panels.

```cpp
void SetPixelLayoutIrregular(PglLayout layoutId,
                             const PglVec2* coords,
                             uint16_t pixelCount,
                             bool reversed = false);

void SetPixelLayoutRect(PglLayout layoutId,
                        uint16_t pixelCount,
                        const PglVec2& size,
                        const PglVec2& position,
                        uint16_t rowCount,
                        uint16_t colCount,
                        bool reversed = false);
```

| Method | Description |
|---|---|
| `SetPixelLayoutIrregular` | Specify per-pixel coordinates explicitly. Use for non-grid panels. |
| `SetPixelLayoutRect` | Describe a rectangular grid layout by row/column counts, size, and origin. |

| Parameter | Type | Description |
|---|---|---|
| `layoutId` | `PglLayout` | Layout index (0–3) |
| `reversed` | `bool` | If `true`, pixel order is reversed |

### 6.14 GPU Memory Access

Low-level memory commands for direct GPU memory manipulation.

```cpp
void MemWrite(PglMemTier tier, uint32_t address,
              const void* data, uint32_t size);

void MemReadRequest(PglMemTier tier, uint32_t address, uint16_t size);

void SetResourceTier(PglMemResourceClass resourceClass,
                     uint16_t resourceId,
                     PglMemTier preferredTier,
                     bool pinned = false);

void MemAlloc(PglMemTier tier, uint32_t size, uint16_t tag = 0);
void MemFree(PglMemHandle handle);

void FramebufferCapture(uint8_t bufferSelect = 0,
                        PglTextureFormat format = PGL_TEX_RGB565);

void MemCopy(PglMemTier srcTier, uint32_t srcAddress,
             PglMemTier dstTier, uint32_t dstAddress,
             uint32_t size);
```

| Method | Description |
|---|---|
| `MemWrite` | Write raw bytes to GPU memory at a specific tier and address. |
| `MemReadRequest` | Stage a region for readback via bidirectional Octal SPI. The data becomes available via the SPI read path (`SPI_READ_MEM_DATA`) after the GPU processes the command. |
| `SetResourceTier` | Hint the GPU's memory manager to place a resource in a preferred tier. If `pinned`, the resource will not be evicted or migrated. |
| `MemAlloc` | Allocate a block on the specified tier. Allocation result is readable via SPI read (`SPI_READ_ALLOC_RESULT`) or I2C fallback. The optional `tag` aids debugging. |
| `MemFree` | Free a previous allocation by handle. |
| `FramebufferCapture` | Capture the current framebuffer contents for SPI readback (via SMW bulk staging). Use `bufferSelect` for double-buffered configs. |
| `MemCopy` | Copy data between tiers on the GPU side without host involvement. Useful for SRAM ↔ PSRAM migration. |

### 6.15 2D Layer & Drawing Commands *(v0.7)*

Layer management and 2D drawing extensions for the compositing pipeline.

```cpp
// Layer management
void SetLayer(uint8_t layerId, PglLayerType type, PglLayerBlend blendMode,
              uint8_t opacity, bool visible, uint8_t renderTargetId,
              uint16_t viewX, uint16_t viewY, uint16_t viewW, uint16_t viewH,
              uint16_t shaderId = PGL_NO_SHADER);
void ClearLayer(uint8_t layerId, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

// 2D primitives
void DrawRect(uint8_t layerId, int16_t x, int16_t y, uint16_t w, uint16_t h,
              uint8_t r, uint8_t g, uint8_t b, bool filled = true, ...);
void DrawLine(uint8_t layerId, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
              uint8_t r, uint8_t g, uint8_t b, uint8_t width = 1);
void DrawCircle(uint8_t layerId, int16_t cx, int16_t cy, uint16_t rx, uint16_t ry,
                uint8_t r, uint8_t g, uint8_t b, bool filled = true, ...);
void DrawSprite(uint8_t layerId, PglTexture textureId,
                int16_t x, int16_t y, uint16_t w, uint16_t h, ...);
void DrawText(uint8_t layerId, int16_t x, int16_t y,
              const char* text, uint8_t textLen, uint8_t r, uint8_t g, uint8_t b, ...);
void DrawTriangle2D(uint8_t layerId, int16_t x0, int16_t y0,
                    int16_t x1, int16_t y1, int16_t x2, int16_t y2, ...);
void DrawGradientRect(uint8_t layerId, int16_t x, int16_t y, uint16_t w, uint16_t h,
                      const PglColor3& topLeft, const PglColor3& topRight,
                      const PglColor3& bottomLeft, const PglColor3& bottomRight);

// Sprite batching
void SpriteBatchBegin(uint8_t layerId, PglTexture textureId,
                      uint16_t spriteCount, PglBlendMode blendMode);
void SpriteBatchEntry(int16_t x, int16_t y,
                      uint16_t srcX, uint16_t srcY, uint16_t srcW, uint16_t srcH,
                      uint8_t opacity = 255, uint8_t flags = 0);
void SpriteBatchEnd();

// Clipping & viewport
void SetClipRect(uint8_t layerId, int16_t x, int16_t y, uint16_t w, uint16_t h, bool enable = true);
void SetViewport(uint8_t renderTargetId, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
```

| Method | Opcode | Description |
|---|---|---|
| `SetLayer` | `0xA0` | Configure a compositing layer (type, blend, opacity, viewport, optional shader binding) |
| `ClearLayer` | `0xAC` | Clear a layer to a solid colour |
| `DrawRect` | `0xA1` | Draw filled/outlined rectangle with optional rounded corners |
| `DrawLine` | `0xA2` | Draw line segment |
| `DrawCircle` | `0xA3` | Draw filled/outlined circle or ellipse |
| `DrawSprite` | `0xA4` | Draw textured sprite from atlas |
| `DrawText` | `0xA5` | Draw text using built-in GPU font |
| `DrawTriangle2D` | `0xA6` | Draw filled 2D triangle |
| `DrawGradientRect` | `0xAE` | Draw gradient-filled rectangle |
| `SpriteBatchBegin` | `0xA7` | Begin sprite batch with shared texture |
| `SpriteBatchEntry` | `0xA8` | Add sprite to batch |
| `SpriteBatchEnd` | `0xA9` | Submit sprite batch |

> See [2D_Graphics_And_Compositing.md](../../../docs/2D_Graphics_And_Compositing.md) for full wire-format structures.

### 6.16 Per-Layer Shader Binding *(v0.7)*

```cpp
void SetLayerShader(uint8_t layerId, uint16_t shaderId,
                    const float* params = nullptr, uint8_t paramCount = 0);
```

| Parameter | Type | Description |
|---|---|---|
| `layerId` | `uint8_t` | Target layer (0–7) |
| `shaderId` | `uint16_t` | PSB shader program ID (`PGL_NO_SHADER` = 0xFFFF to unbind) |
| `params` | `const float*` | Optional array of float parameters loaded into shader registers r16–r31 |
| `paramCount` | `uint8_t` | Number of float parameters (0–16) |

Emits `PGL_OP_SET_LAYER_SHADER` (0xB0). The shader runs per-pixel on the layer's
framebuffer **after** all 2D draw commands complete and **before** compositing.

### 6.17 Shared Memory Window (Bidirectional Access) *(v0.7)*

```cpp
void SmwWriteMailbox(uint8_t slot, uint32_t value);
void SmwReadMailbox(uint32_t outSlots[16]);
void SmwStageRead(uint32_t gpuAddress, uint16_t size, bool irqNotify = true);
void SmwReadBulk(void* dst, uint16_t size);
uint32_t SmwGetSequence();
```

| Method | Opcode | Description |
|---|---|---|
| `SmwWriteMailbox` | `0x42` | Write a 32-bit value to a Host→GPU mailbox slot (0–15) |
| `SmwReadMailbox` | `0x43` | Read all 16 GPU→Host mailbox slots via bidirectional Octal SPI |
| `SmwStageRead` | `0x44` | Request GPU to copy memory into the 3840-byte bulk staging buffer |
| `SmwReadBulk` | — | Read the bulk staging buffer contents (SPI RX half-duplex read, no opcode) |
| `SmwGetSequence` | — | Read the SMW sequence counter for change detection |

### 6.18 Resource Persistence & Direct Framebuffer Write *(v0.7.1)*

Commands for persisting GPU resources across power cycles and writing raw pixel
data directly to the output framebuffer.

```cpp
// --- Persistence ---
void PersistResource(PglMemResourceClass resourceClass,
                     uint16_t resourceId,
                     uint8_t flags = 0x00);

void RestoreResource(PglMemResourceClass resourceClass,
                     uint16_t resourceId,
                     uint8_t flags = PGL_RESTORE_AUTO_ALLOC);

void QueryPersistence(PglMemResourceClass resourceClass,
                      uint16_t resourceId,
                      uint8_t flags = 0x00);

void ErasePersisted(PglMemResourceClass resourceClass,
                    uint16_t resourceId);

// --- Direct Framebuffer Write ---
void WriteFramebuffer(uint16_t x, uint16_t y,
                      uint16_t w, uint16_t h,
                      uint8_t layerId,
                      const void* pixels,
                      uint32_t size);
```

| Method | Opcode | Description |
|---|---|---|
| `PersistResource` | `0x46` | Mark a resource for persistence. On PSRAM, triggers background writeback to GPU flash (512 KB reserved, 4 KB/frame incremental). On MRAM, updates metadata only (zero-cost — already non-volatile). `flags`: reserved (set 0). |
| `RestoreResource` | `0x47` | Restore a previously persisted resource after reboot. GPU scans flash manifest, re-allocates VRAM, and copies data back. `PGL_RESTORE_AUTO_ALLOC` = 0x01: GPU picks matching tier automatically. |
| `QueryPersistence` | `0x48` | Query persistence status. `flags = 0x00`: per-resource query (12-byte response: status, flashAddr, sizeBytes). `flags = PGL_PERSIST_QUERY_MANIFEST` (0x01): manifest summary (totalEntries, usedEntries, freeBytes). Result available via I2C register `MEM_PERSIST_STATUS` (0x1C) or SPI read `SPI_READ_PERSIST_STATUS` (0xEB). |
| `ErasePersisted` | — | Remove a resource from flash persistence. Encoded as `PersistResource` with erase flag. Resource remains in VRAM until explicitly destroyed. |
| `WriteFramebuffer` | `0x45` | Write raw RGB565 pixel data directly to the GPU back buffer (or a compositor layer). `layerId = 0xFF`: writes to default output FB. `layerId = 0–7`: writes to compositor layer buffer. Executes during command parsing phase — if same frame has `DrawObject`, 3D renders first, then direct-write regions overwrite. |

**Persistence status codes** (returned via `QueryPersistence`):

| Value | Name | Meaning |
|---|---|---|
| `0x00` | `PGL_PERSIST_NONE` | Resource is not persisted |
| `0x01` | `PGL_PERSIST_IN_PROGRESS` | Flash writeback currently running |
| `0x02` | `PGL_PERSIST_COMPLETE` | Resource is persisted in flash (or MRAM) |
| `0x03` | `PGL_PERSIST_ERROR` | Writeback failed (flash full, CRC error) |

**Flash manifest** (GPU-internal):

| Field | Description |
|---|---|
| `PGL_FLASH_MANIFEST_ADDR` | `0x10380000` — 512 KB reserved at end of 4 MB flash |
| Max entries | 64 |
| Entry size | 16 bytes (`PglFlashManifestEntry`: resourceClass, resourceId, flashOffset, sizeBytes, crc32, flags) |
| Header | 16 bytes (`PglFlashManifestHeader`: magic, version, entryCount, crc32) |

**I2C register**: `MEM_PERSIST_STATUS` (0x1C) — 12-byte response with persistence query results.

**SPI read**: `SPI_READ_PERSIST_STATUS` (0xEB) — same data via bidirectional Octal SPI.

**Mailbox slot**: `PGL_MBOX_GPU_PERSIST_STATUS` (Slot 9) — GPU writes completion notification when background writeback finishes.

The Shared Memory Window provides **1000×+ faster** GPU→Host reads compared to I2C,
using the bidirectional Octal SPI bus (DIR pin half-duplex turnaround).
See [Memory_Management_API.md §9.5](../../../docs/Memory_Management_API.md) for the full protocol.

---

## 7. PglDevice.h — Host Device Driver

`PglDevice` manages the bidirectional Octal SPI transport and optional I2C fallback bus.
It owns a double-buffered command buffer and handles asynchronous DMA submission.

### 7.1 PglDeviceConfig

```cpp
struct PglDeviceConfig {
    int8_t  spiDataPins[8];        // Octal SPI D0–D7 (default: all -1)
    int8_t  spiClkPin;             // SPI clock (default: -1)
    int8_t  spiCsPin;              // SPI chip-select (default: -1)
    uint8_t spiClockMHz;           // 40, 64, or 80 (default: 80)
    int8_t  dirPin;                // Bus direction pin — LOW=Host TX, HIGH=GPU TX (default: -1)
    int8_t  irqPin;                // GPU→Host IRQ notification (active-low, default: -1)
    int8_t  i2cSdaPin;             // I2C SDA — optional fallback (default: -1)
    int8_t  i2cSclPin;             // I2C SCL — optional fallback (default: -1)
    uint8_t i2cAddress;            // GPU I2C address (default: 0x3C, only if I2C enabled)
    uint32_t commandBufferSize;    // Buffer size bytes (default: 32768)
    uint8_t i2cPort;               // I2C bus: 0=Wire, 1=Wire1 (default: 0)
    uint16_t dirTurnaroundCycles;  // Dummy clocks during DIR turnaround (default: 2)
};
```

**Pin assignment notes:**

- All pin fields default to `-1` (unconfigured). You must set at least the SPI and
  `dirPin` before calling `Initialize()`. I2C pins are optional (fallback only).
- `dirPin` controls the Octal SPI bus direction (replaces the legacy RDY pin).
  The same physical wire that was previously used for flow control (RDY) now
  serves as the half-duplex direction signal.
- `irqPin` receives GPU→Host async notifications (SMW mailbox updates, alloc
  completions, error events). This single IRQ line is shared across both the
  Octal SPI and I2C protocols. If omitted, the host must poll for status changes.

### 7.2 PglDevice Lifecycle

```cpp
bool Initialize(const PglDeviceConfig& config);
void Destroy();
bool IsInitialized() const;
PglEncoder* GetEncoder();
```

| Method | Description |
|---|---|
| `Initialize` | Allocate double-buffers (`config.commandBufferSize × 2`), configure ESP32 LCD peripheral for 8-bit parallel SPI, and initialise I2C. Returns `false` on memory allocation failure. |
| `Destroy` | Tear down SPI/I2C, free buffers. Safe to call multiple times. |
| `IsInitialized` | Returns `true` after a successful `Initialize()` call. |
| `GetEncoder` | Returns a pointer to the `PglEncoder` targeting the current active buffer. Only valid between `BeginFrame()` and `EndFrame()`. |

### 7.3 Frame Lifecycle

```cpp
void BeginFrame(uint32_t frameNumber, uint32_t frameTimeUs);
void EndFrame();
```

| Method | Description |
|---|---|
| `BeginFrame` | Swap the ping-pong buffer, wait for previous DMA (if in-flight), reset the encoder, and call `encoder->BeginFrame()`. |
| `EndFrame` | Call `encoder->EndFrame()`, then submit the buffer asynchronously via DMA. If a read transaction is in progress (DIR=HIGH), the frame is queued until the bus is available. |

**Double-buffering:** While frame N is being transmitted over DMA, the host can
immediately begin recording frame N+1 into the alternate buffer.

### 7.4 Configuration

Configuration methods write to GPU registers via **Octal SPI commands** (preferred)
or I2C (fallback). Query methods use SPI read transactions (0xE0–0xEA) for high-speed
GPU→Host data.

| Method | Signature | Description |
|---|---|---|
| `SetBrightness` | `void SetBrightness(uint8_t brightness)` | Write 1 byte to `PGL_REG_BRIGHTNESS` |
| `SetPanelConfig` | `void SetPanelConfig(uint16_t width, uint16_t height)` | Set panel resolution (REG_PANEL_WIDTH + REG_PANEL_HEIGHT) |
| `SetScanRate` | `void SetScanRate(uint8_t scanRate)` | Write 1 byte to `PGL_REG_SCAN_RATE` |
| `ClearDisplay` | `void ClearDisplay()` | Write `0x01` to `PGL_REG_CLEAR` |
| `SetGammaTable` | `void SetGammaTable(uint8_t table)` | Select gamma table (0 = linear, 1 = 2.2, etc.) |
| `ResetGPU` | `void ResetGPU()` | Write `0x52` to `PGL_REG_RESET`; GPU re-initialises |
| `SetClockFrequency` | `void SetClockFrequency(uint16_t targetMHz, uint8_t voltageLevel = 0, uint8_t flags = PGL_CLOCK_RECONFIGURE_PIO)` | Request dynamic frequency change |

**Query methods:**

| Method | Returns | Description |
|---|---|---|
| `QueryStatus()` | `PglStatusResponse` | 8-byte status (ready/rendering/error, current frame, queue depth) |
| `QueryCapability()` | `PglCapabilityResponse` | 16-byte capabilities (arch, cores, memory, feature flags) |
| `QueryExtendedStatus()` | `PglExtendedStatusResponse` | 32-byte detailed status (CPU %, temp, VRAM usage, timing) |
| `HasExternalVram()` | `bool` | Shorthand: queries capabilities and checks `PGL_CAP_EXTERNAL_VRAM` (RP2350B only) |

### 7.5 Frame Statistics

| Method | Returns | Description |
|---|---|---|
| `GetDroppedFrames()` | `uint32_t` | Total frames dropped because GPU was not ready |
| `GetOverflowFrames()` | `uint32_t` | Total frames dropped due to buffer overflow |
| `GetGpuStalls()` | `uint32_t` | Number of times consecutive drops exceeded `kMaxConsecutiveDrops` (10) |
| `GetConsecutiveDrops()` | `uint32_t` | Current run of sequential dropped frames |
| `IsGpuReady()` | `bool` | Reads GPU status via SPI_READ_STATUS; returns true if GPU is idle |

---

## 8. PglParser.h — Command Buffer Parser

GPU-side alignment-safe utilities for reading the binary command stream. All functions
advance the pointer reference unless otherwise noted.

### Template Functions

```cpp
template<typename T> inline T PglRead(const uint8_t*& ptr);
template<typename T> inline T PglPeek(const uint8_t* ptr);
template<typename T> inline void PglReadStruct(const uint8_t*& ptr, T& out);
template<typename T> inline void PglPeekStruct(const uint8_t* ptr, T& out);
template<typename T> inline void PglReadArray(const uint8_t*& ptr, T* dest, uint16_t count);
```

| Function | Description |
|---|---|
| `PglRead<T>` | Read a value of type `T` from an unaligned byte stream. Advances `ptr` by `sizeof(T)`. |
| `PglPeek<T>` | Same as `PglRead` but does **not** advance the pointer. |
| `PglReadStruct<T>` | Deserialise a packed struct via `memcpy`. Advances `ptr` by `sizeof(T)`. |
| `PglPeekStruct<T>` | Deserialise without advancing. |
| `PglReadArray<T>` | Copy `count` elements of type `T` from stream to `dest`. Advances `ptr` by `count * sizeof(T)`. |

### Standalone Functions

```cpp
inline void PglSkip(const uint8_t*& ptr, size_t bytes);
inline int32_t PglFindSyncWord(const uint8_t* data, size_t length);
inline bool PglValidateFrameCRC(const uint8_t* frameStart, uint32_t totalLength);
```

| Function | Description |
|---|---|
| `PglSkip` | Advance read pointer by `bytes` without reading. |
| `PglFindSyncWord` | Scan a buffer for `PGL_SYNC_WORD` (0x55AA, little-endian). Returns byte offset or `-1` if not found. Used for frame resynchronisation. |
| `PglValidateFrameCRC` | Validate the CRC-16 of a complete frame (header + payload + trailing CRC). Returns `true` if valid. |

---

## 9. PglShaderBackend.h — Shader Math Backend

Platform-portable math primitives used by the GPU's shader VM. All functions are
`static inline` inside the `PglShaderBackend` namespace. The active implementation is
selected at compile time.

### 9.1 Compile-Time Defines

| Define | Description |
|---|---|
| `PGL_BACKEND_SCALAR_FLOAT` | Default — standard `<cmath>` functions |
| `PGL_BACKEND_CM33_FPV5` | Cortex-M33 FPv5: single-cycle `fmaf`, hardware `VSQRT` |
| `PGL_BACKEND_CM33_DSP` | Cortex-M33 DSP: saturating half-word pixel ops |
| `PGL_BACKEND_SOFT_FLOAT` | Integer-only approximation for FPU-less cores |

Define exactly one before including `PglShaderBackend.h`. On the RP2350 GPU the build
system sets `PGL_BACKEND_CM33_FPV5`.

### 9.2 Arithmetic

| Function | Signature | Description |
|---|---|---|
| `Add` | `float Add(float a, float b)` | $a + b$ |
| `Sub` | `float Sub(float a, float b)` | $a - b$ |
| `Mul` | `float Mul(float a, float b)` | $a \times b$ |
| `Neg` | `float Neg(float a)` | $-a$ |
| `Div` | `float Div(float a, float b)` | $a / b$ (returns 0 if $b = 0$) |
| `Fma` | `float Fma(float a, float b, float c)` | $a \times b + c$ (fused multiply-add on CM33) |

### 9.3 Math Functions

| Function | Signature | Description |
|---|---|---|
| `Sin` | `float Sin(float x)` | $\sin(x)$ — parabolic approximation on soft-float |
| `Cos` | `float Cos(float x)` | $\cos(x)$ |
| `Tan` | `float Tan(float x)` | $\tan(x)$ |
| `Asin` | `float Asin(float x)` | $\arcsin(x)$ — input clamped to $[-1, 1]$ |
| `Acos` | `float Acos(float x)` | $\arccos(x)$ |
| `Atan` | `float Atan(float x)` | $\arctan(x)$ |
| `Atan2` | `float Atan2(float y, float x)` | $\text{atan2}(y, x)$ |
| `Pow` | `float Pow(float base, float exp)` | $|base|^{exp}$ (exp trick on soft-float) |
| `Exp` | `float Exp(float x)` | $e^x$ (Schraudolph trick on soft-float) |
| `Log` | `float Log(float x)` | $\ln(x)$ (int bit trick on soft-float) |
| `Sqrt` | `float Sqrt(float a)` | $\sqrt{a}$ (VSQRT instruction on CM33) |
| `Rsqrt` | `float Rsqrt(float a)` | $1/\sqrt{a}$ (fast inverse sqrt on soft-float) |

### 9.4 Rounding & Value Manipulation

| Function | Signature | Description |
|---|---|---|
| `Abs` | `float Abs(float x)` | $|x|$ |
| `Floor` | `float Floor(float x)` | $\lfloor x \rfloor$ |
| `Ceil` | `float Ceil(float x)` | $\lceil x \rceil$ |
| `Sign` | `float Sign(float x)` | $-1$, $0$, or $+1$ |
| `Fract` | `float Fract(float x)` | $x - \lfloor x \rfloor$ |
| `Mod` | `float Mod(float x, float y)` | $x \bmod y$ (returns 0 if $y = 0$) |

### 9.5 Clamping & Interpolation

| Function | Signature | Description |
|---|---|---|
| `Min` | `float Min(float a, float b)` | $\min(a, b)$ |
| `Max` | `float Max(float a, float b)` | $\max(a, b)$ |
| `Clamp` | `float Clamp(float x, float lo, float hi)` | Clamp $x$ to $[lo, hi]$ |
| `Mix` | `float Mix(float a, float b, float t)` | $a + t(b - a)$ — linear interpolation |
| `Step` | `float Step(float edge, float x)` | 0 if $x < edge$, else 1 |
| `Smoothstep` | `float Smoothstep(float edge0, float edge1, float x)` | Hermite interpolation: $3t^2 - 2t^3$ where $t = \text{clamp}((x-e_0)/(e_1-e_0))$ |

### 9.6 Geometric (2-D / 3-D)

| Function | Signature | Description |
|---|---|---|
| `Dot2` | `float Dot2(float ax, float ay, float bx, float by)` | 2-D dot product |
| `Dot3` | `float Dot3(float ax, float ay, float az, float bx, float by, float bz)` | 3-D dot product |
| `Len2` | `float Len2(float x, float y)` | 2-D vector length $\sqrt{x^2+y^2}$ |
| `Len3` | `float Len3(float x, float y, float z)` | 3-D vector length |
| `Norm2` | `void Norm2(float x, float y, float& outX, float& outY)` | 2-D normalise |
| `Norm3` | `void Norm3(float x, float y, float z, float& outX, float& outY, float& outZ)` | 3-D normalise |
| `Cross` | `void Cross(ax, ay, az, bx, by, bz, &outX, &outY, &outZ)` | 3-D cross product |
| `Dist2` | `float Dist2(float ax, float ay, float bx, float by)` | 2-D Euclidean distance |

### 9.7 Texture Sampling & Pixel Packing

| Function | Signature | Description |
|---|---|---|
| `UnpackRGB565` | `void UnpackRGB565(uint16_t pixel, float& r, float& g, float& b)` | Unpack RGB565 → float [0, 1] per channel |
| `PackRGB565` | `uint16_t PackRGB565(float r, float g, float b)` | Pack float RGB → RGB565 |
| `PackRGB565i` | `uint16_t PackRGB565i(uint8_t r5, uint8_t g6, uint8_t b5)` | Pack integer 5/6/5 channels |
| `TexSample` | `void TexSample(const uint16_t* fb, uint16_t w, uint16_t h, float u, float v, float& outR, float& outG, float& outB)` | Nearest-neighbour framebuffer sample at UV $\in [0,1]$ |
| `R5` | `uint8_t R5(uint16_t c)` | Extract 5-bit red from RGB565 |
| `G6` | `uint8_t G6(uint16_t c)` | Extract 6-bit green from RGB565 |
| `B5` | `uint8_t B5(uint16_t c)` | Extract 5-bit blue from RGB565 |
| `Clamp5` | `uint8_t Clamp5(int v)` | Clamp to $[0, 31]$ |
| `Clamp6` | `uint8_t Clamp6(int v)` | Clamp to $[0, 63]$ |

**Soft-float constants** (only defined under `PGL_BACKEND_SOFT_FLOAT`):

| Constant | Value |
|---|---|
| `PI_F` | 3.14159265… |
| `TWO_PI` | 6.28318530… |
| `HALF_PI` | 1.57079632… |

---

## 10. PglShaderBytecode.h — PSB Format

**PGL Shader Bytecode (PSB)** is the binary format for compiled programmable shaders.
It is shared between the host (compiler output) and the GPU (shader VM input).

### 10.1 Constants

| Name | Value | Description |
|---|---|---|
| `PSB_MAGIC` | `0x50534231` | "PSB1" magic number |
| `PSB_VERSION` | `1` | Format version |
| `PSB_MAX_UNIFORMS` | `16` | Max uniforms per program |
| `PSB_MAX_CONSTANTS` | `32` | Max constants per program |
| `PSB_MAX_INSTRUCTIONS` | `256` | Max instructions per program |
| `PSB_NUM_REGISTERS` | `32` | Register file size |
| `PSB_MAX_PROGRAM_SIZE` | `1296` | Max binary size in bytes |
| `PSB_FLAG_NEEDS_SCRATCH_COPY` | `0x01` | Shader reads from the framebuffer (requires scratch copy) |

### 10.2 Operand Encoding

Each instruction source/destination operand is an 8-bit value:

| Range | Name | Description |
|---|---|---|
| `0x00–0x1F` | `PSB_OP_REG_BASE..END` | Register r0–r31 |
| `0x20–0x2F` | `PSB_OP_UNIFORM_BASE..END` | Uniform u0–u15 |
| `0x30–0x4F` | `PSB_OP_CONST_BASE..END` | Constant c0–c31 |
| `0x50–0x5F` | `PSB_OP_LITERAL_BASE..END` | Inline literal (see table) |
| `0xFF` | `PSB_OP_UNUSED` | Unused / no operand |

### 10.3 Inline Literal Table

`PSB_LITERALS[]` — 16 common float constants encoded in a single byte:

| Operand | Value | | Operand | Value |
|---|---|---|---|---|
| `0x50` | `0.0` | | `0x58` | $1/2\pi$ |
| `0x51` | `0.5` | | `0x59` | $1/\pi$ |
| `0x52` | `1.0` | | `0x5A` | `0.01` |
| `0x53` | `2.0` | | `0x5B` | `0.1` |
| `0x54` | `-1.0` | | `0x5C` | `10.0` |
| `0x55` | `-0.5` | | `0x5D` | `100.0` |
| `0x56` | $\pi$ | | `0x5E` | $1/\sqrt{2}$ |
| `0x57` | $2\pi$ | | `0x5F` | $\sqrt{2}$ |

### 10.4 VM Opcodes

Each instruction is 4 bytes: `[opcode, dst, srcA, srcB]`.

#### Special

| Opcode | Mnemonic | Semantics |
|---|---|---|
| `0x00` | `NOP` | No operation |
| `0xFF` | `END` | Halt; output r28–r31 |

#### Arithmetic

| Opcode | Mnemonic | Semantics |
|---|---|---|
| `0x01` | `MOV` | `dst = srcA` |
| `0x02` | `ADD` | `dst = srcA + srcB` |
| `0x03` | `SUB` | `dst = srcA − srcB` |
| `0x04` | `MUL` | `dst = srcA × srcB` |
| `0x05` | `DIV` | `dst = srcA / srcB` (0 if srcB = 0) |
| `0x06` | `FMA` | `dst = srcA × srcB + dst` |
| `0x07` | `NEG` | `dst = −srcA` |

#### Math Functions

| Opcode | Mnemonic | Semantics |
|---|---|---|
| `0x10` | `SIN` | `dst = sin(srcA)` |
| `0x11` | `COS` | `dst = cos(srcA)` |
| `0x12` | `TAN` | `dst = tan(srcA)` |
| `0x13` | `ASIN` | `dst = asin(srcA)` |
| `0x14` | `ACOS` | `dst = acos(srcA)` |
| `0x15` | `ATAN` | `dst = atan(srcA)` |
| `0x16` | `ATAN2` | `dst = atan2(srcA, srcB)` |
| `0x17` | `POW` | `dst = pow(srcA, srcB)` |
| `0x18` | `EXP` | `dst = exp(srcA)` |
| `0x19` | `LOG` | `dst = log(srcA)` |
| `0x1A` | `SQRT` | `dst = sqrt(srcA)` |
| `0x1B` | `RSQRT` | `dst = 1/sqrt(srcA)` |
| `0x1C` | `ABS` | `dst = abs(srcA)` |
| `0x1D` | `SIGN` | `dst = sign(srcA)` |
| `0x1E` | `FLOOR` | `dst = floor(srcA)` |
| `0x1F` | `CEIL` | `dst = ceil(srcA)` |
| `0x20` | `FRACT` | `dst = fract(srcA)` |
| `0x21` | `MOD` | `dst = mod(srcA, srcB)` |

#### Clamping / Interpolation

| Opcode | Mnemonic | Semantics |
|---|---|---|
| `0x30` | `MIN` | `dst = min(srcA, srcB)` |
| `0x31` | `MAX` | `dst = max(srcA, srcB)` |
| `0x32` | `CLAMP` | `dst = clamp(srcA, srcB, dst)` — lo = srcB, hi = dst |
| `0x33` | `MIX` | `dst = mix(srcA, srcB, dst)` — t = dst |
| `0x34` | `STEP` | `dst = step(srcA, srcB)` |
| `0x35` | `SSTEP` | `dst = smoothstep(srcA, srcB, dst)` |

> **Note:** 3-operand instructions (CLAMP, MIX, SSTEP) overload `dst` as both input
> and output. Ensure the destination register holds the third operand before execution.

#### Geometric (Vector)

| Opcode | Mnemonic | Semantics |
|---|---|---|
| `0x40` | `DOT2` | `dst = dot(srcA..+1, srcB..+1)` — uses 2 consecutive registers |
| `0x41` | `DOT3` | `dst = dot(srcA..+2, srcB..+2)` — uses 3 consecutive registers |
| `0x42` | `LEN2` | `dst = length(srcA, srcA+1)` |
| `0x43` | `LEN3` | `dst = length(srcA..+2)` |
| `0x44` | `NORM2` | `dst, dst+1 = normalize(srcA, srcA+1)` |
| `0x45` | `NORM3` | `dst..+2 = normalize(srcA..+2)` |
| `0x46` | `CROSS` | `dst..+2 = cross(srcA..+2, srcB..+2)` |
| `0x47` | `DIST2` | `dst = distance(srcA..+1, srcB..+1)` |

#### Texture Sampling

| Opcode | Mnemonic | Semantics |
|---|---|---|
| `0x50` | `TEX2D` | `dst..+3 = texture2D(fb, srcA, srcA+1)` → RGBA |

#### Constant / Uniform Load

| Opcode | Mnemonic | Semantics |
|---|---|---|
| `0x60` | `LCONST` | `dst = constants[srcA]` |
| `0x61` | `LUNI` | `dst = uniforms[srcA]` |

### 10.5 Register Map

| Register(s) | Name | Binding |
|---|---|---|
| r0 | `PSB_REG_FRAG_X` | `gl_FragCoord.x` |
| r1 | `PSB_REG_FRAG_Y` | `gl_FragCoord.y` |
| r2 | `PSB_REG_FRAG_Z` | 0.0 (z) |
| r3 | `PSB_REG_FRAG_W` | 1.0 (w) |
| r4 | `PSB_REG_IN_R` | Current pixel red [0, 1] |
| r5 | `PSB_REG_IN_G` | Current pixel green [0, 1] |
| r6 | `PSB_REG_IN_B` | Current pixel blue [0, 1] |
| r7 | `PSB_REG_IN_A` | 1.0 (alpha, reserved) |
| r8–r27 | `PSB_REG_USER_START`–`END` | 20 user temporaries |
| r28 | `PSB_REG_OUT_R` | Output red (`gl_FragColor.r`) |
| r29 | `PSB_REG_OUT_G` | Output green |
| r30 | `PSB_REG_OUT_B` | Output blue |
| r31 | `PSB_REG_OUT_A` | Output alpha |

### 10.6 Auto-Bound Uniforms

| Slot | Name | Binding |
|---|---|---|
| 0 | `PSB_AUTO_UNIFORM_RESOLUTION_X` | `u_resolution.x` (pixels) |
| 1 | `PSB_AUTO_UNIFORM_RESOLUTION_Y` | `u_resolution.y` (pixels) |
| 2 | `PSB_AUTO_UNIFORM_TIME` | `u_time` (elapsed seconds, float) |
| 3+ | `PSB_USER_UNIFORM_START` | First user-assignable slot |

### 10.7 Structs & Enums

#### `PglShaderProgramHeader` (16 bytes, packed)

| Field | Type | Description |
|---|---|---|
| `magic` | `uint32_t` | `PSB_MAGIC` (0x50534231) |
| `version` | `uint8_t` | `PSB_VERSION` (1) |
| `flags` | `uint8_t` | Bitfield (see `PSB_FLAG_*`) |
| `constCount` | `uint8_t` | Number of constants |
| `uniformCount` | `uint8_t` | Number of uniforms |
| `instrCount` | `uint16_t` | Number of instructions |
| `nameHash` | `uint32_t` | FNV-1a hash of program name |
| `reserved` | `uint16_t` | — |

#### `PglUniformDescriptor` (8 bytes, packed)

| Field | Type | Description |
|---|---|---|
| `nameHash` | `uint32_t` | FNV-1a hash of uniform name |
| `type` | `PglUniformType` | `FLOAT`, `VEC2`, `VEC3`, or `VEC4` |
| `slot` | `uint8_t` | Slot index (0–15) |
| `defaultValueOffset` | `uint16_t` | Offset into constant pool for default value |

#### `PglShaderInstruction` (4 bytes)

| Field | Type | Description |
|---|---|---|
| `opcode` | `uint8_t` | One of the `PSB_OP_*` values |
| `dst` | `uint8_t` | Destination operand |
| `srcA` | `uint8_t` | Source operand A |
| `srcB` | `uint8_t` | Source operand B |

#### `PglUniformType : uint8_t`

| Value | Name |
|---|---|
| 0 | `PSB_UNIFORM_FLOAT` |
| 1 | `PSB_UNIFORM_VEC2` |
| 2 | `PSB_UNIFORM_VEC3` |
| 3 | `PSB_UNIFORM_VEC4` |

### 10.8 Standalone Functions

```cpp
static inline uint32_t PsbFnv1a(const char* str);
static inline float PsbResolveOperand(uint8_t op,
                                       const float* regs,
                                       const float* uniforms,
                                       const float* constants);
```

| Function | Description |
|---|---|
| `PsbFnv1a` | FNV-1a 32-bit hash. Used for uniform name lookup without string comparison. |
| `PsbResolveOperand` | Decode an 8-bit operand into a float value, consulting the register file, uniform array, constant pool, or inline literal table as appropriate. |

---

## 11. PglShaderCompiler.h — PGLSL Compiler

The PGLSL compiler transforms a GLSL-like source language into PSB bytecode.
It runs entirely on the **ESP32-S3 host** and produces a binary blob suitable for
`PglEncoder::CreateShaderProgram()`.

### 11.1 CompileResult

```cpp
struct CompileResult {
    bool     success;
    uint8_t  bytecode[PSB_MAX_PROGRAM_SIZE];  // 1296 bytes
    uint16_t bytecodeSize;
    char     errorMsg[128];
    uint16_t errorLine;
};
```

| Field | Description |
|---|---|
| `success` | `true` if compilation succeeded |
| `bytecode` | The compiled PSB binary. Only valid if `success == true`. |
| `bytecodeSize` | Size of the valid portion of `bytecode`. |
| `errorMsg` | Human-readable error message (only on failure). |
| `errorLine` | Source line number of the error (1-based). |

### 11.2 Public API

```cpp
static CompileResult Compile(const char* source, size_t sourceLength);
```

| Parameter | Type | Description |
|---|---|---|
| `source` | `const char*` | Null-terminated PGLSL source code |
| `sourceLength` | `size_t` | Length of `source` in bytes (excluding null terminator) |

**Returns:** `CompileResult` with the compiled bytecode or error details.

**PGLSL language subset:**

```glsl
#version 100
precision mediump float;
uniform float u_myParam;

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;
    vec3 col = gl_FragColor.rgb;
    col *= u_myParam;
    gl_FragColor = vec4(col, 1.0);
}
```

**Supported features:**
- Types: `float`, `vec2`, `vec3`, `vec4`, `sampler2D`
- Declarations: `uniform`, `precision`
- Expressions: arithmetic (`+ - * /`), negation, swizzles (`.xyzw`, `.rgba`, `.stpq`)
- Constructors: `vec2(…)`, `vec3(…)`, `vec4(…)`
- Built-in functions: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `pow`, `exp`,
  `log`, `sqrt`, `abs`, `sign`, `floor`, `ceil`, `fract`, `mod`, `min`, `max`,
  `clamp`, `mix`, `step`, `smoothstep`, `length`, `distance`, `dot`, `cross`,
  `normalize`, `texture2D`
- Built-in variables: `gl_FragCoord`, `gl_FragColor`, `u_resolution`, `u_time`,
  `u_framebuffer`

**Limitations:**
- No `if`/`else`, loops, or function definitions (beyond `main`)
- Max 256 instructions, 32 constants, 16 uniforms, 48 variables
- Max 2048 tokens, 512 AST nodes

### 11.3 Compiler Internals (private)

The compiler pipeline runs in six phases:

1. **Lexer** (`Lex`) — tokenises source into up to `MAX_TOKENS` (2048) tokens
2. **Parser** (`Parse`) — builds an AST of up to `MAX_NODES` (512) nodes
3. **Type Checker** — validates types during code generation
4. **Register Allocator** — maps variables to the 20 user registers (r8–r27)
5. **Code Generator** (`GenerateCode`) — walks the AST and emits `PglShaderInstruction`s
6. **Assembler** (`AssembleBinary`) — packs header + constants + uniforms + instructions

**Key internal types:**

| Type | Description |
|---|---|
| `Token` | Lexer token with type, source position, line number, numeric value |
| `ASTNode` | AST node with type, value type, literal value, name, swizzle info, children |
| `VarInfo` | Variable record: name, type, register, uniform flag, uniform slot |
| `UniformInfo` | Uniform record: name, type, slot, FNV-1a hash |

**Internal limits:**

| Constant | Value |
|---|---|
| `MAX_TOKENS` | 2048 |
| `MAX_NODES` | 512 |
| `MAX_STMTS` | 128 |
| `MAX_VARS` | 48 |
| `MAX_INSTRS` | 256 |

---

## 12. PglJobScheduler.h — Job Scheduler Interface

Abstract interface for platform-portable parallel job dispatch on the GPU.

### PglJob

```cpp
struct PglJob {
    void (*func)(void* ctx);
    void* ctx;
};
```

| Field | Type | Description |
|---|---|---|
| `func` | `void (*)(void*)` | Job function pointer |
| `ctx` | `void*` | Opaque caller-owned context passed to `func` |

### PglJobScheduler (Abstract)

```cpp
class PglJobScheduler {
public:
    virtual ~PglJobScheduler() = default;
    virtual uint8_t WorkerCount() const = 0;
    virtual void Submit(const PglJob* jobs, uint8_t count) = 0;
    virtual void WaitAll(void (*idleFunc)() = nullptr) = 0;
};
```

| Method | Description |
|---|---|
| `WorkerCount()` | Number of worker cores/threads **excluding** the caller. Returns 0 for single-core. |
| `Submit(jobs, count)` | Distribute `count` jobs across available workers. Some may execute inline on the calling core. |
| `WaitAll(idleFunc)` | Block until all submitted jobs complete. Optional `idleFunc` is called while spinning, useful for feeding watchdog timers. |

---

## 13. PglJobScheduler_SingleCore.h — Serial Fallback

Serial fallback scheduler — executes all jobs immediately on the calling core.
Used on single-core platforms or as a baseline for testing.

### PglJobScheduler_SingleCore

```cpp
class PglJobScheduler_SingleCore : public PglJobScheduler {
public:
    uint8_t WorkerCount() const override;   // returns 0
    void Submit(const PglJob* jobs, uint8_t count) override;
    void WaitAll(void (*idleFunc)()) override;
};
```

| Method | Description |
|---|---|
| `WorkerCount()` | Always returns `0`. |
| `Submit(jobs, count)` | Executes all jobs serially in a loop on the calling core. |
| `WaitAll(idleFunc)` | No-op — all jobs already completed during `Submit`. |

---

## 14. Quick Cross-Reference

### By Task

| Task | Primary API | Module |
|---|---|---|
| Record a frame | `PglDevice::BeginFrame/EndFrame` | PglDevice |
| Encode commands | `PglEncoder::*` | PglEncoder |
| Upload mesh | `PglEncoder::CreateMesh` | PglEncoder |
| Upload texture | `PglEncoder::CreateTexture` | PglEncoder |
| Update texture pixels | `PglEncoder::UpdateTexture` | PglEncoder |
| Upload image sequence | `PglEncoder::CreateImageSequence` | PglEncoder |
| Upload custom font | `PglEncoder::CreateFont` | PglEncoder |
| Set camera | `PglEncoder::SetCamera` | PglEncoder |
| Draw 3-D object | `PglEncoder::DrawObject` | PglEncoder |
| Apply post-process | `SetConvolution`, `SetDisplacement`, etc. | PglEncoder |
| Compile PGLSL | `PglShaderCompiler::Compile` | PglShaderCompiler |
| Upload custom shader | `PglEncoder::CreateShaderProgram` | PglEncoder |
| Set shader uniform | `PglEncoder::SetShaderUniform` | PglEncoder |
| Query GPU status | `PglDevice::QueryStatus` | PglDevice |
| Change brightness | `PglDevice::SetBrightness` | PglDevice |
| Direct memory write | `PglEncoder::MemWrite` | PglEncoder |
| Streaming upload | `PglEncoder::MemStreamBegin/Chunk/End` | PglEncoder |
| Persist resource | `PglEncoder::PersistResource` | PglEncoder |
| Restore resource | `PglEncoder::RestoreResource` | PglEncoder |
| Query persistence | `PglEncoder::QueryPersistence` | PglEncoder |
| Write framebuffer | `PglEncoder::WriteFramebuffer` | PglEncoder |
| Validate frame CRC | `PglValidateFrameCRC` | PglParser |

### Resource Limits

| Resource | Max | Constant |
|---|---|---|
| Cameras | 4 | `PGL_MAX_CAMERAS` |
| Draw calls/frame | 64 | `PGL_MAX_DRAW_CALLS` |
| Shader slots/camera | 8 | `PGL_MAX_SHADER_SLOTS` |
| Meshes | 32 | `PGL_MAX_MESHES` |
| Materials | 32 | `PGL_MAX_MATERIALS` |
| Textures | 64 | `PGL_MAX_TEXTURES` |
| Image Sequences | 32 | `PGL_MAX_IMAGE_SEQUENCES` |
| Custom Fonts | 16 | `PGL_MAX_FONTS` |
| Shader Programs | 16 | `PGL_MAX_SHADER_PROGRAMS` |
| Layouts | 4 | `PGL_MAX_LAYOUTS` |
| Vertices/mesh | 4096 | `PGL_MAX_VERTICES` |
| Triangles/mesh | 8192 | `PGL_MAX_TRIANGLES` |
| Shader uniforms | 16 | `PSB_MAX_UNIFORMS` |
| Shader constants | 32 | `PSB_MAX_CONSTANTS` |
| Shader instructions | 256 | `PSB_MAX_INSTRUCTIONS` |
| Shader registers | 32 | `PSB_NUM_REGISTERS` |
| Shader binary size | 1296 B | `PSB_MAX_PROGRAM_SIZE` |
| Flash manifest entries | 64 | `PGL_MAX_PERSIST_ENTRIES` |
| Flash persistence area | 512 KB | `PGL_FLASH_MANIFEST_ADDR = 0x10380000` |

---

*Document auto-generated from ProtoGL v0.7.1 source headers.  
See also: `docs/ProtoGL_API_Spec.md` for wire-format details,
`docs/ProtoGL_Usage_And_Examples.md` for code samples.*
