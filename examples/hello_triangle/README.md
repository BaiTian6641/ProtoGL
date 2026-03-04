# Hello Triangle — ProtoGL Minimal Example

The simplest possible ProtoGL program: an **ESP32-S3** sends a single magenta triangle
to an **RP2350 GPU** over **Octal SPI at 80 MHz**, which rasterizes it onto a
**64 × 64 HUB75 LED matrix**.

This is the ProtoGL equivalent of the classic
[Vulkan Hello Triangle](https://vulkan-tutorial.com/Drawing_a_triangle) tutorial.

## What It Does

```
  ESP32-S3                    RP2350 GPU
 ┌─────────┐   Octal SPI    ┌──────────┐   HUB75    ┌───────────┐
 │  Host    │ ──80 MHz────▶  │ Rasterize│ ─────────▶ │ 64×64     │
 │ ProtoGL  │   8-bit bus    │ + Z-buf  │            │ LED Panel │
 │ Encoder  │   (DMA)        │ dual-core│            │           │
 │          │ ◀──I2C──────── │ 150 MHz  │            │           │
 │          │   400 kHz      │ ARM CM33 │            │           │
 └─────────┘                 └──────────┘            └───────────┘
```

### Per-Frame Pipeline

1. `gpu.BeginFrame()` — swap ping-pong command buffer
2. `enc->SetCamera()` — 2D orthographic camera, identity transform
3. `enc->DrawObject()` — draw mesh 0 with material 0
4. `gpu.EndFrame()` — finalize CRC-16, DMA transfer to GPU

The GPU receives the frame, parses commands, rasterizes the triangle with
barycentric interpolation and Z-buffer, evaluates the `PGL_MAT_SIMPLE` material,
and outputs to the HUB75 panel via BCM (Binary Code Modulation).

## Hardware Wiring

### Octal SPI Data Bus (ESP32-S3 LCD Parallel Mode → RP2350)

| Signal | ESP32-S3 GPIO | RP2350 Pin | Description          |
|--------|:-------------:|:----------:|----------------------|
| D0     | 6             | GPx        | Data bit 0           |
| D1     | 7             | GPx        | Data bit 1           |
| D2     | 15            | GPx        | Data bit 2           |
| D3     | 16            | GPx        | Data bit 3           |
| D4     | 17            | GPx        | Data bit 4           |
| D5     | 18            | GPx        | Data bit 5           |
| D6     | 8             | GPx        | Data bit 6           |
| D7     | 3             | GPx        | Data bit 7           |
| CLK    | 12            | GPx        | Write-clock (80 MHz) |
| CS     | 11            | GPx        | Chip-select (active low) |

### I2C Control Bus

| Signal | ESP32-S3 GPIO | RP2350 Pin | Description          |
|--------|:-------------:|:----------:|----------------------|
| SDA    | 1             | GPx        | I2C data (400 kHz)   |
| SCL    | 2             | GPx        | I2C clock            |

### Flow Control

| Signal | ESP32-S3 GPIO | RP2350 Pin | Description          |
|--------|:-------------:|:----------:|----------------------|
| RDY    | 9             | GPx        | GPU ready (active high, output from RP2350) |

> **Note:** Replace `GPx` with the actual RP2350 GPIO numbers for your PCB layout.
> The ESP32-S3 GPIOs above are examples — adjust the `#define` lines at the top
> of `hello_triangle.ino` to match your board.

### Power

| Rail    | Voltage | Notes                              |
|---------|:-------:|------------------------------------|
| ESP32-S3 | 3.3 V | USB or regulator                   |
| RP2350   | 3.3 V | Shared regulated supply            |
| HUB75    | 5.0 V | Separate supply, 2 A+ recommended  |

Both chips share a 3.3 V domain — no level shifters needed.

## Triangle Geometry

```
        v0 (0, 20)
        /\
       /  \
      /    \
     /  ■■  \        ← magenta fill (RGB 255, 0, 255)
    /________\
v1 (-24, -16)  v2 (24, -16)
```

World coordinates, 2D camera (orthographic projection).  
The 64 × 64 layout maps X ∈ [−32, 32], Y ∈ [−32, 32].

## Building

### PlatformIO (recommended)

This example lives inside the ProtoGL library. To build it as a standalone
PlatformIO project, create a `platformio.ini`:

```ini
[env:esp32s3]
platform  = espressif32
board     = esp32-s3-devkitc-1
framework = arduino
lib_deps  = ProtoGL
build_flags =
    -std=gnu++17
    -DARDUINO_ARCH_ESP32
monitor_speed = 115200
```

### Arduino IDE

1. Copy `hello_triangle.ino` into a folder named `hello_triangle/`
2. Install **ESP32** board support (Espressif Arduino Core ≥ 3.0)
3. Select board: **ESP32-S3 Dev Module**
4. Install the **ProtoGL** library (copy `lib/ProtoGL/` into your `libraries/` folder)
5. Upload and open Serial Monitor at 115200 baud

## Expected Output

### Serial Monitor

```
ProtoGL — Hello Triangle
PglDevice initialized — Octal SPI + I2C ready
GPU arch: 0x01  cores: 2  freq: 150 MHz  SRAM: 520 KB
Max vertices: 2048  triangles: 1024  meshes: 256  materials: 256
Resources uploaded — entering render loop
Frame 60  GPU FPS: 120  dropped: 0  overflow: 0
Frame 120  GPU FPS: 120  dropped: 0  overflow: 0
```

### Display

A solid magenta triangle centered on a black background on the 64 × 64 HUB75 panel.

## API Walkthrough

| Step | API Call | Purpose |
|------|----------|---------|
| 1 | `PglDevice::Initialize(cfg)` | Set up Octal SPI DMA + I2C bus |
| 2 | `QueryCapability()` | Discover GPU architecture and limits |
| 3 | `SetBrightness(64)` | Configure HUB75 panel brightness via I2C |
| 4 | `SetPixelLayoutRect(...)` | Tell GPU: "64×64 rectangular pixel grid" |
| 5 | `CreateMesh(0, verts, 3, idx, 1)` | Upload 3 vertices + 1 triangle |
| 6 | `CreateMaterial(0, SIMPLE, ...)` | Solid magenta (255, 0, 255) |
| 7 | `BeginFrame()` / `EndFrame()` | Frame lifecycle (ping-pong DMA) |
| 8 | `SetCamera(0, 0, ..., is2D=true)` | 2D orthographic camera |
| 9 | `DrawObject(0, 0, ...)` | Draw mesh 0 with material 0 |

## Next Steps

- Change the material color by modifying the `PglParamSimple` RGB values
- Add rotation: pass a non-identity `PglQuat` to `DrawObject()`
- Add a second triangle with a different material
- Switch to a `PGL_MAT_GRADIENT` material for vertex-colored output
- Add a `PGL_MAT_LIGHT` material with a directional light
- Upload a texture with `CreateTexture()` and use a `PGL_MAT_IMAGE` material

## License

MIT — see the root [LICENSE](../../../../LICENSE) file.
