/**
 * @file hello_triangle.ino
 * @brief ProtoGL "Hello Triangle" — minimal example drawing a single colored
 *        triangle on a 64×64 HUB75 panel driven by an RP2350 GPU.
 *
 * Hardware:
 *   ESP32-S3  ──(Octal SPI @ 80 MHz)──▶  RP2350 GPU  ──▶  64×64 HUB75
 *             ──(I2C @ 400 kHz)──────────▶  (config/status)
 *
 * This is the ProtoGL equivalent of the classic Vulkan "Hello Triangle":
 * the absolute minimum needed to get a rendered frame on screen.
 *
 * Steps:
 *   1. Configure PglDevice (Octal SPI + I2C pins)
 *   2. Upload a pixel layout   (tells the GPU about the physical display)
 *   3. Upload a mesh            (3 vertices, 1 triangle)
 *   4. Upload a material        (solid magenta)
 *   5. Each frame: begin → set camera → draw → end
 *
 * ProtoGL API Specification v0.5
 */

#include <Arduino.h>
#include <ProtoGL.h>

// ─── Pin Configuration ──────────────────────────────────────────────────────
// Adjust these to match YOUR wiring between the ESP32-S3 and the RP2350.
// The 8 data pins form the Octal SPI bus (ESP32-S3 LCD parallel mode).

#define PGL_D0   6
#define PGL_D1   7
#define PGL_D2  15
#define PGL_D3  16
#define PGL_D4  17
#define PGL_D5  18
#define PGL_D6   8
#define PGL_D7   3

#define PGL_CLK 12      // Write-clock (WR)
#define PGL_CS  11      // Chip-select (directly to RP2350 CS)

#define PGL_SDA  1      // I2C data  (config / status bus)
#define PGL_SCL  2      // I2C clock

#define PGL_RDY  9      // GPU ready signal (active-high input)

// ─── Display Geometry ───────────────────────────────────────────────────────

static constexpr uint16_t PANEL_W  = 64;
static constexpr uint16_t PANEL_H  = 64;
static constexpr uint16_t PIXELS   = PANEL_W * PANEL_H; // 4 096

// ─── Resource IDs ───────────────────────────────────────────────────────────
// ProtoGL identifies resources by small integer handles, just like Vulkan.

static constexpr PglMesh     MESH_TRIANGLE = 0;
static constexpr PglMaterial MAT_SOLID     = 0;
static constexpr PglLayout   LAYOUT_PANEL  = 0;
static constexpr PglCamera   CAM_MAIN      = 0;

// ─── Globals ────────────────────────────────────────────────────────────────

PglDevice gpu;

uint32_t frameNumber = 0;
uint32_t lastFrameUs = 0;

// ─── Helper: identity quaternion ────────────────────────────────────────────

static constexpr PglQuat QUAT_IDENTITY = { 1.0f, 0.0f, 0.0f, 0.0f };
static constexpr PglVec3 VEC3_ZERO     = { 0.0f, 0.0f, 0.0f };
static constexpr PglVec3 VEC3_ONE      = { 1.0f, 1.0f, 1.0f };

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("ProtoGL — Hello Triangle");

    // ── 1. Configure the device ─────────────────────────────────────────

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
    cfg.spiClockMHz = 80;         // 80 MHz Octal SPI

    cfg.i2cSdaPin   = PGL_SDA;
    cfg.i2cSclPin   = PGL_SCL;
    cfg.i2cAddress  = PGL_I2C_DEFAULT_ADDR;  // 0x3C

    cfg.rdyPin      = PGL_RDY;

    cfg.commandBufferSize = 4096; // 4 KB is plenty for a single triangle

    if (!gpu.Initialize(cfg)) {
        Serial.println("ERROR: PglDevice::Initialize() failed!");
        while (true) { delay(1000); }
    }

    Serial.println("PglDevice initialized — Octal SPI + I2C ready");

    // ── 2. (Optional) Query GPU capabilities ────────────────────────────

    PglCapabilityResponse cap = gpu.QueryCapability();
    Serial.printf("GPU arch: 0x%02X  cores: %u  freq: %u MHz  SRAM: %u KB\n",
                  cap.gpuArch, cap.coreCount, cap.coreFreqMHz, cap.sramKB);
    Serial.printf("Max vertices: %u  triangles: %u  meshes: %u  materials: %u\n",
                  cap.maxVertices, cap.maxTriangles, cap.maxMeshes, cap.maxMaterials);

    // ── 3. Set brightness (I2C) ─────────────────────────────────────────

    gpu.SetBrightness(64); // moderate brightness (0-255)

    // ── 4. Upload pixel layout (tells GPU about the display geometry) ───

    // A single 64×64 rectangular grid, origin at center.
    // The GPU maps camera output to these pixel coordinates.
    gpu.BeginFrame(frameNumber, 0);
    PglEncoder* enc = gpu.GetEncoder();

    PglVec2 panelSize = { static_cast<float>(PANEL_W),
                          static_cast<float>(PANEL_H) };
    PglVec2 panelPos  = { 0.0f, 0.0f };  // centered at origin

    enc->SetPixelLayoutRect(LAYOUT_PANEL, PIXELS,
                            panelSize, panelPos,
                            PANEL_H, PANEL_W);

    // ── 5. Create triangle mesh ─────────────────────────────────────────
    //
    //        v0 (top center)
    //        /\
    //       /  \
    //      /    \
    //   v1 ──── v2
    //
    // Coordinates are in world space.  With a 2D camera and a 64×64 layout
    // centered at origin, X ∈ [-32, 32] and Y ∈ [-32, 32].

    PglVec3 vertices[3] = {
        {   0.0f,  20.0f, 0.0f },   // v0 — top center
        { -24.0f, -16.0f, 0.0f },   // v1 — bottom-left
        {  24.0f, -16.0f, 0.0f },   // v2 — bottom-right
    };

    PglIndex3 indices[1] = {
        { 0, 1, 2 },                // one triangle, CCW winding
    };

    enc->CreateMesh(MESH_TRIANGLE,
                    vertices, 3,
                    indices, 1);

    // ── 6. Create a solid-color material (magenta) ──────────────────────

    PglParamSimple magenta = { 255, 0, 255 };

    enc->CreateMaterial(MAT_SOLID,
                        PGL_MAT_SIMPLE,
                        PGL_BLEND_BASE,
                        &magenta, sizeof(magenta));

    // Commit the resource-upload frame
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

    // ── Set camera (2D orthographic, identity transform) ────────────────

    enc->SetCamera(CAM_MAIN, LAYOUT_PANEL,
                   VEC3_ZERO,         // position  — origin
                   QUAT_IDENTITY,     // rotation  — none
                   VEC3_ONE,          // scale     — 1:1
                   QUAT_IDENTITY,     // lookOffset
                   QUAT_IDENTITY,     // baseRotation
                   true);             // is2D = true → orthographic

    // ── Draw the triangle ───────────────────────────────────────────────

    enc->DrawObject(MESH_TRIANGLE, MAT_SOLID,
                    VEC3_ZERO,         // position  (world origin)
                    QUAT_IDENTITY,     // rotation
                    VEC3_ONE,          // scale
                    QUAT_IDENTITY,     // baseRotation
                    QUAT_IDENTITY,     // scaleRotationOffset
                    VEC3_ZERO,         // scaleOffset
                    VEC3_ZERO,         // rotationOffset
                    true);             // enabled

    gpu.EndFrame();
    frameNumber++;

    // ── Print FPS every 60 frames ───────────────────────────────────────

    if (frameNumber % 60 == 0) {
        PglStatusResponse st = gpu.QueryStatus();
        Serial.printf("Frame %lu  GPU FPS: %u  dropped: %u  overflow: %lu\n",
                      frameNumber, st.currentFPS, st.droppedFrames,
                      gpu.GetOverflowFrames());
    }
}
