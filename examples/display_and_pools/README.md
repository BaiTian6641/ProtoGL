# Display & Memory Pools — ProtoGL M11 Example

Demonstrates the two key **M11 milestone** features:
- **Display abstraction** — `DisplayConfigure` command, `QueryDisplayCaps`, multi-display routing
- **Memory pools** — `MemPoolCreate`/`MemPoolAlloc`/`MemPoolFree`/`MemPoolDestroy`

## What It Does

```
  ESP32-S3                    RP2350 GPU
 ┌─────────┐   Octal SPI    ┌──────────┐   HUB75    ┌───────────┐
 │  Host    │ ──80 MHz────▶  │ Display  │ ─────────▶ │ 64×64     │
 │ ProtoGL  │   8-bit bus    │ Manager  │            │ LED Panel │
 │ Encoder  │   (DMA)        │          │   I2C1     │           │
 │          │ ◀──I2C──────── │ MemPool  │ ─────────▶ ┌───────────┐
 │          │   400 kHz      │ Allocator│            │ 128×64    │
 └─────────┘                 └──────────┘            │ HUD OLED  │
                                                     └───────────┘
```

### Setup Phase

1. `gpu.Initialize()` — configure Octal SPI + I2C transport
2. `gpu.QueryDisplayCaps()` — discover what display drivers are available
3. `enc->DisplayConfigure(0, HUB75, ...)` — configure HUB75 as primary output
4. `enc->DisplayConfigure(1, I2C_HUD, ...)` — enable HUD OLED for GPU status overlay
5. `enc->MemPoolCreate(SRAM, 256, 16, ...)` — create a 16×256B block pool in SRAM
6. Upload mesh, materials, pixel layout

### Render Loop

- Draws a rotating magenta/cyan triangle (alternates color every 120 frames)
- At frame 60: allocates 4 pool blocks (demonstrates `MemPoolAlloc`)
- At frame 120: frees 4 pool blocks (demonstrates `MemPoolFree`)
- Every 60 frames: prints GPU FPS + pool status via I2C queries

## Display Abstraction (M11)

M11 extracts a `DisplayDriver` abstract base class from the existing HUB75 driver:

```
DisplayDriver (ABC)
├── Hub75Driver     — PIO0 HUB75 LED matrix (primary)
├── I2cHudDriver    — I2C1 SSD1306/SSD1309 128×64 OLED (status HUD)
├── QspiLcdDriver   — (future M12)
└── DviDriver       — (future M12)
```

### Display Commands

| Command | Opcode | Purpose |
|---------|--------|---------|
| `CMD_DISPLAY_CONFIGURE` | 0x90 | Configure display driver slot |
| `CMD_DISPLAY_SET_REGION` | 0x91 | Set partial update region |

### I2C Registers

| Register | ID | Purpose |
|----------|----|---------|
| `DISPLAY_MODE` | 0x15 | Select/query active display |
| `DISPLAY_CAPS` | 0x16 | Read display capabilities (16 bytes) |

### PglDisplayCaps

```cpp
struct PglDisplayCaps {
    uint8_t  displayType;   // PGL_DISPLAY_HUB75, PGL_DISPLAY_I2C_HUD, ...
    uint16_t width, height; // Native resolution
    uint8_t  pixelFormat;   // PGL_PIXFMT_RGB565, PGL_PIXFMT_MONO1, ...
    uint8_t  maxBrightness; // 255 = full range
    uint8_t  flags;         // Supported feature flags
    uint16_t refreshHz;     // Native refresh rate
    uint16_t framebufKB;    // Framebuffer size
    uint8_t  pioUsage;      // PIO state machines consumed
    uint8_t  dmaUsage;      // DMA channels consumed
};  // 16 bytes
```

## Memory Pools (M11)

Fixed-size block allocator with O(1) alloc/free and zero fragmentation:

```
┌──────────────────────────────────────────────────┐
│  Pool (16 × 256 B in SRAM)                       │
│  ┌──────┬──────┬──────┬──────┬──────┬───┬──────┐ │
│  │ B[0] │ B[1] │ B[2] │ B[3] │ B[4] │...│B[15]│ │
│  └──────┴──────┴──────┴──────┴──────┴───┴──────┘ │
│  Free list: 0 → 1 → 2 → ... → 15 → NULL         │
└──────────────────────────────────────────────────┘
```

### Memory Pool Commands

| Command | Opcode | Purpose |
|---------|--------|---------|
| `CMD_MEM_POOL_CREATE`  | 0x38 | Create fixed-size block pool |
| `CMD_MEM_POOL_ALLOC`   | 0x39 | Pop one block (O(1)) |
| `CMD_MEM_POOL_FREE`    | 0x3A | Push one block back (O(1)) |
| `CMD_MEM_POOL_DESTROY` | 0x3B | Destroy pool, return memory |

### I2C Register

| Register | ID | Purpose |
|----------|----|---------|
| `MEM_POOL_STATUS` | 0x18 | Query pool statistics (12 bytes) |

## Hardware Wiring

Same as [hello_triangle](../hello_triangle/README.md) — see that README for pinout tables.

### Optional: I2C HUD OLED

| Signal | RP2350 Pin | OLED Pin | Notes |
|--------|:----------:|:--------:|-------|
| SDA1   | GPIO 14    | SDA      | I2C1 data (shared with host management bus) |
| SCL1   | GPIO 15    | SCL      | I2C1 clock |
| VCC    | 3.3V       | VCC      | SSD1306/SSD1309, address 0x3D (A0 = VCC) |
| GND    | GND        | GND      | |

> The I2C HUD OLED shares the I2C1 bus with the host management slave at 0x3C.
> The HUD driver uses address 0x3D.

## Building

### PlatformIO

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

1. Copy `display_and_pools.ino` into a folder named `display_and_pools/`
2. Install **ESP32** board support (Espressif Arduino Core >= 3.0)
3. Select board: **ESP32-S3 Dev Module**
4. Install the **ProtoGL** library (copy `lib/ProtoGL/` into your `libraries/` folder)
5. Upload and open Serial Monitor at 115200 baud

## Expected Output

### Serial Monitor

```
ProtoGL M11 — Display Abstraction + Memory Pools
PglDevice initialized
GPU: arch=0x01  cores=2  freq=150 MHz  SRAM=520 KB
External VRAM: NO
Display 0: type=0x01  64x64  fmt=0  refresh=333 Hz  PIO=2  DMA=2
Display 1: type=0x02  I2C HUD OLED available
I2C HUD OLED enabled (auto GPU status overlay)
Memory pool created (16 × 256 B in SRAM, tag=0x0001)
Resources uploaded — entering render loop
Frame 60  GPU FPS: 120  dropped: 0  overflow: 0
Pool: allocated 4 blocks
Pool 0: 12/16 blocks free  blockSize=256  tier=0
Frame 120  GPU FPS: 120  dropped: 0  overflow: 0
Pool: freed 4 blocks
Pool 0: 16/16 blocks free  blockSize=256  tier=0
```

### Display

A rotating triangle on the 64×64 HUB75 panel, alternating between magenta and cyan every 2 seconds.
If a HUD OLED is connected, it displays live GPU stats (FPS, VRAM, temperature, CPU%).

## API Walkthrough

| Step | API Call | Purpose |
|------|----------|---------|
| 1 | `PglDevice::Initialize(cfg)` | Set up Octal SPI DMA + I2C bus |
| 2 | `QueryCapability()` | Discover GPU architecture and limits |
| 3 | `QueryDisplayCaps(0)` | Read HUB75 display capabilities |
| 4 | `DisplayConfigure(0, HUB75, ...)` | Configure HUB75 as display slot 0 |
| 5 | `DisplayConfigure(1, I2C_HUD, ...)` | Enable I2C HUD OLED as slot 1 |
| 6 | `MemPoolCreate(SRAM, 256, 16)` | Create 16×256B block pool |
| 7 | `SetPixelLayoutRect(...)` | Define 64×64 pixel grid |
| 8 | `CreateMesh(0, ...)` | Upload triangle geometry |
| 9 | `CreateMaterial(0, ...)` | Solid magenta |
| 10 | `CreateMaterial(1, ...)` | Solid cyan |
| 11 | `MemPoolAlloc(0)` | Allocate pool block (demo, frame 60) |
| 12 | `MemPoolFree(0, i)` | Free pool block (demo, frame 120) |
| 13 | `QueryMemPoolStatus(0)` | Read pool stats via I2C |

## Next Steps

- Create pools in external VRAM tiers (`PGL_TIER_QSPI_A` / `PGL_TIER_QSPI_B`)
- Use `DisplaySetRegion()` for partial screen updates
- Query extended status via `QueryExtendedStatus()`
- Add a `PGL_DISPLAY_SPI_LCD` driver (M12)
- Implement `MemPoolDestroy()` for pool teardown
- Use pool blocks for dynamic texture uploads

## License

MIT — see the root [LICENSE](../../../../LICENSE) file.
