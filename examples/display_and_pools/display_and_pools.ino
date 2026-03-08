/**
 * @file display_and_pools.ino
 * @brief ProtoGL M11 example — display abstraction + memory pools.
 *
 * Demonstrates the two key M11 features:
 *   1. Display driver abstraction (DisplayConfigure, QueryDisplayCaps)
 *   2. Memory pool allocation (MemPoolCreate, MemPoolAlloc, MemPoolFree)
 *
 * Hardware:
 *   ESP32-S3  ──(Octal SPI @ 80 MHz)──▶  RP2350 GPU  ──▶  64×64 HUB75
 *             ──(I2C @ 400 kHz)──────────▶  (config/status)
 *             (Optional) RP2350 I2C1 ──▶  128×64 SSD1306 OLED (HUD)
 *
 * This example:
 *   1. Configures PglDevice
 *   2. Queries GPU capabilities and display capabilities
 *   3. Configures HUB75 display via DisplayConfigure command
 *   4. (Optional) Enables I2C HUD OLED for GPU status overlay
 *   5. Creates a memory pool and demonstrates alloc/free cycle
 *   6. Renders a rotating triangle to prove display abstraction works
 *
 * ProtoGL API Specification v0.7
 */

#include <Arduino.h>
#include <ProtoGL.h>
#include <math.h>

// ─── Pin Configuration ──────────────────────────────────────────────────────
// Same as hello_triangle — adjust for your board.

#define PGL_D0   6
#define PGL_D1   7
#define PGL_D2  15
#define PGL_D3  16
#define PGL_D4  17
#define PGL_D5  18
#define PGL_D6   8
#define PGL_D7   3

#define PGL_CLK 12
#define PGL_CS  11

#define PGL_SDA  1
#define PGL_SCL  2

#define PGL_DIR  10
#define PGL_IRQ  13

// ─── Display Geometry ───────────────────────────────────────────────────────

static constexpr uint16_t PANEL_W  = 64;
static constexpr uint16_t PANEL_H  = 64;
static constexpr uint16_t PIXELS   = PANEL_W * PANEL_H;

// ─── Resource / Handle IDs ──────────────────────────────────────────────────

static constexpr PglMesh     MESH_TRI     = 0;
static constexpr PglMaterial MAT_MAGENTA  = 0;
static constexpr PglMaterial MAT_CYAN     = 1;
static constexpr PglLayout   LAYOUT_PANEL = 0;
static constexpr PglCamera   CAM_MAIN     = 0;

static constexpr PglDisplay  DISP_HUB75   = 0;
static constexpr PglDisplay  DISP_HUD     = 1;

// ─── Globals ────────────────────────────────────────────────────────────────

PglDevice gpu;

uint32_t frameNumber = 0;
uint32_t lastFrameUs = 0;

// Pool demo state
bool     poolDemoComplete = false;
uint32_t poolDemoFrame    = 0;

// ─── Constants ──────────────────────────────────────────────────────────────

static constexpr PglQuat QUAT_IDENTITY = { 1.0f, 0.0f, 0.0f, 0.0f };
static constexpr PglVec3 VEC3_ZERO     = { 0.0f, 0.0f, 0.0f };
static constexpr PglVec3 VEC3_ONE      = { 1.0f, 1.0f, 1.0f };

// ─── Helper: build a Z-axis rotation quaternion ─────────────────────────────

static PglQuat QuatFromAngle(float angleDeg) {
    float rad = angleDeg * (3.14159265f / 180.0f);
    return { cosf(rad * 0.5f), 0.0f, 0.0f, sinf(rad * 0.5f) };
}

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("ProtoGL M11 — Display Abstraction + Memory Pools");

    // ── 1. Configure PglDevice ──────────────────────────────────────────

    PglDeviceConfig cfg{};

    cfg.spiDataPins[0] = PGL_D0;
    cfg.spiDataPins[1] = PGL_D1;
    cfg.spiDataPins[2] = PGL_D2;
    cfg.spiDataPins[3] = PGL_D3;
    cfg.spiDataPins[4] = PGL_D4;
    cfg.spiDataPins[5] = PGL_D5;
    cfg.spiDataPins[6] = PGL_D6;
    cfg.spiDataPins[7] = PGL_D7;

    cfg.spiClkPin   = PGL_CLK;
    cfg.spiCsPin    = PGL_CS;
    cfg.spiClockMHz = 80;

    cfg.i2cSdaPin   = PGL_SDA;
    cfg.i2cSclPin   = PGL_SCL;
    cfg.i2cAddress  = PGL_I2C_DEFAULT_ADDR;

    cfg.dirPin      = PGL_DIR;
    cfg.irqPin      = PGL_IRQ;

    cfg.commandBufferSize = 8192;  // 8 KB — enough for display + pool commands

    if (!gpu.Initialize(cfg)) {
        Serial.println("ERROR: PglDevice::Initialize() failed!");
        while (true) { delay(1000); }
    }
    Serial.println("PglDevice initialized");

    // ── 2. Query GPU and display capabilities ───────────────────────────

    PglCapabilityResponse cap = gpu.QueryCapability();
    Serial.printf("GPU: arch=0x%02X  cores=%u  freq=%u MHz  SRAM=%u KB\n",
                  cap.gpuArch, cap.coreCount, cap.coreFreqMHz, cap.sramKB);

    bool hasVram = gpu.HasExternalVram();
    Serial.printf("External VRAM: %s\n", hasVram ? "YES" : "NO");

    // Query display slot 0 (HUB75)
    PglDisplayCaps hub75Caps = gpu.QueryDisplayCaps(DISP_HUB75);
    Serial.printf("Display 0: type=0x%02X  %ux%u  fmt=%u  refresh=%u Hz  PIO=%u  DMA=%u\n",
                  hub75Caps.displayType, hub75Caps.width, hub75Caps.height,
                  hub75Caps.pixelFormat, hub75Caps.refreshHz,
                  hub75Caps.pioUsage, hub75Caps.dmaUsage);

    // Query display slot 1 (HUD — may be empty if no OLED attached)
    PglDisplayCaps hudCaps = gpu.QueryDisplayCaps(DISP_HUD);
    bool hudAvailable = (hudCaps.displayType != PGL_DISPLAY_NONE);
    Serial.printf("Display 1: type=0x%02X  %s\n",
                  hudCaps.displayType,
                  hudAvailable ? "I2C HUD OLED available" : "not present");

    // ── 3. Configure displays via command buffer ────────────────────────

    gpu.BeginFrame(frameNumber, 0);
    PglEncoder* enc = gpu.GetEncoder();

    // Display slot 0: HUB75 panel (primary output)
    enc->DisplayConfigure(DISP_HUB75, PGL_DISPLAY_HUB75,
                          PANEL_W, PANEL_H,
                          PGL_PIXFMT_RGB565,
                          PGL_DISP_FLAG_ENABLED,
                          80);  // brightness

    // Display slot 1: I2C HUD OLED (auto GPU status overlay)
    if (hudAvailable) {
        enc->DisplayConfigure(DISP_HUD, PGL_DISPLAY_I2C_HUD,
                              128, 64,
                              PGL_PIXFMT_MONO1,
                              PGL_DISP_FLAG_ENABLED | PGL_DISP_FLAG_HUD_AUTO,
                              255);
        Serial.println("I2C HUD OLED enabled (auto GPU status overlay)");
    }

    gpu.EndFrame();
    frameNumber++;

    // ── 4. Create a memory pool and demonstrate alloc/free ──────────────
    //
    // M11 introduces MemPool — a fixed-size block allocator backed by a
    // contiguous allocation from a GPU memory tier. O(1) alloc and free
    // with zero fragmentation.
    //
    // Here we create a pool of 16 × 256-byte blocks in SRAM, allocate
    // a few blocks, write some data, and free them back.

    gpu.BeginFrame(frameNumber, 0);
    enc = gpu.GetEncoder();

    // Create a pool: 16 blocks of 256 bytes each in SRAM (tier 0)
    enc->MemPoolCreate(PGL_TIER_SRAM, 256, 16, 0x0001);  // tag=0x0001

    gpu.EndFrame();
    frameNumber++;

    Serial.println("Memory pool created (16 × 256 B in SRAM, tag=0x0001)");

    // After the GPU processes CMD_MEM_POOL_CREATE, the pool handle is
    // available via PGL_REG_MEM_ALLOC_RESULT I2C register. We'll query
    // status in the render loop (after a few frames for the GPU to process).

    // ── 5. Upload resources ─────────────────────────────────────────────

    gpu.BeginFrame(frameNumber, 0);
    enc = gpu.GetEncoder();

    // Pixel layout
    PglVec2 panelSize = { static_cast<float>(PANEL_W),
                          static_cast<float>(PANEL_H) };
    PglVec2 panelPos  = { 0.0f, 0.0f };
    enc->SetPixelLayoutRect(LAYOUT_PANEL, PIXELS,
                            panelSize, panelPos,
                            PANEL_H, PANEL_W);

    // Triangle mesh (same geometry as hello_triangle)
    PglVec3 vertices[3] = {
        {   0.0f,  20.0f, 0.0f },
        { -24.0f, -16.0f, 0.0f },
        {  24.0f, -16.0f, 0.0f },
    };
    PglIndex3 indices[1] = { { 0, 1, 2 } };
    enc->CreateMesh(MESH_TRI, vertices, 3, indices, 1);

    // Two materials: magenta and cyan
    PglParamSimple magenta = { 255, 0, 255 };
    PglParamSimple cyan    = { 0, 255, 255 };
    enc->CreateMaterial(MAT_MAGENTA, PGL_MAT_SIMPLE, PGL_BLEND_BASE,
                        &magenta, sizeof(magenta));
    enc->CreateMaterial(MAT_CYAN, PGL_MAT_SIMPLE, PGL_BLEND_BASE,
                        &cyan, sizeof(cyan));

    gpu.EndFrame();
    frameNumber++;

    Serial.println("Resources uploaded — entering render loop");
    lastFrameUs = micros();
}

// ─────────────────────────────────────────────────────────────────────────────
// loop()
// ─────────────────────────────────────────────────────────────────────────────

void loop() {
    uint32_t nowUs = micros();
    uint32_t deltaUs = nowUs - lastFrameUs;
    lastFrameUs = nowUs;

    gpu.BeginFrame(frameNumber, deltaUs);
    PglEncoder* enc = gpu.GetEncoder();

    // ── Set camera (2D orthographic) ────────────────────────────────────

    enc->SetCamera(CAM_MAIN, LAYOUT_PANEL,
                   VEC3_ZERO, QUAT_IDENTITY, VEC3_ONE,
                   QUAT_IDENTITY, QUAT_IDENTITY, true);

    // ── Draw a rotating triangle ────────────────────────────────────────
    //   Alternates material colour every 120 frames.

    float angle = static_cast<float>(frameNumber) * 1.5f;  // 1.5°/frame
    PglQuat rotation = QuatFromAngle(angle);
    PglMaterial currentMat = ((frameNumber / 120) % 2 == 0) ? MAT_MAGENTA : MAT_CYAN;

    enc->DrawObject(MESH_TRI, currentMat,
                    VEC3_ZERO, rotation, VEC3_ONE,
                    QUAT_IDENTITY, QUAT_IDENTITY,
                    VEC3_ZERO, VEC3_ZERO, true);

    // ── Memory pool demo: alloc/free exercise ───────────────────────────
    //   At frame 60, allocate 4 blocks. At frame 120, free them.
    //   This proves the pool round-trips through the command buffer.

    if (!poolDemoComplete) {
        if (frameNumber == 60) {
            // Allocate 4 blocks from the pool
            for (int i = 0; i < 4; i++) {
                enc->MemPoolAlloc(0);  // pool handle 0 (first pool created)
            }
            Serial.println("Pool: allocated 4 blocks");
        }
        if (frameNumber == 120) {
            // Free the 4 blocks back
            for (uint16_t i = 0; i < 4; i++) {
                enc->MemPoolFree(0, i);  // pool handle 0, block index i
            }
            Serial.println("Pool: freed 4 blocks");
            poolDemoComplete = true;
        }
    }

    gpu.EndFrame();
    frameNumber++;

    // ── Periodic status reporting ───────────────────────────────────────

    if (frameNumber % 60 == 0) {
        PglStatusResponse st = gpu.QueryStatus();
        Serial.printf("Frame %lu  GPU FPS: %u  dropped: %u  overflow: %lu\n",
                      frameNumber, st.currentFPS, st.droppedFrames,
                      gpu.GetOverflowFrames());

        // Query pool status every 60 frames
        PglMemPoolStatusResponse poolSt = gpu.QueryMemPoolStatus(0);
        if (poolSt.status != PGL_POOL_INVALID_HANDLE) {
            Serial.printf("Pool 0: %u/%u blocks free  blockSize=%u  tier=%u\n",
                          poolSt.freeCount, poolSt.blockCount,
                          poolSt.blockSize, poolSt.tier);
        }
    }
}
