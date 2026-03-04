/**
 * @file PglDevice.h
 * @brief ProtoGL device — manages the Octal SPI transport and I2C control bus
 *        for communication with the RP2350 GPU.
 *
 * This is the high-level host-side interface. It owns the command buffer,
 * manages the encoder lifecycle, and coordinates DMA transfers.
 *
 * ProtoGL API Specification v0.3 — FROZEN
 */

#pragma once

#include "PglTypes.h"
#include "PglOpcodes.h"
#include "PglEncoder.h"
#include "PglCRC16.h"

#include <cstdint>
#include <cstring>

// ─── Device Configuration ───────────────────────────────────────────────────

struct PglDeviceConfig {
    // Octal SPI pins (ESP32-S3 LCD parallel mode)
    int8_t  spiDataPins[8] = {-1,-1,-1,-1,-1,-1,-1,-1}; // D0-D7
    int8_t  spiClkPin      = -1;
    int8_t  spiCsPin       = -1;
    uint8_t spiClockMHz    = 80;      // 40, 64, or 80

    // I2C pins
    int8_t  i2cSdaPin      = -1;
    int8_t  i2cSclPin      = -1;
    uint8_t i2cAddress     = PGL_I2C_DEFAULT_ADDR;

    // Flow control
    int8_t  rdyPin         = -1;       // RP2350 ready signal (input, active high)

    // Command buffer sizing
    uint32_t commandBufferSize = 32768; // 32 KB default — enough for complex frames
};

// ─── Device ─────────────────────────────────────────────────────────────────

class PglDevice {
public:
    PglDevice() = default;
    ~PglDevice() { Destroy(); }

    // Non-copyable
    PglDevice(const PglDevice&) = delete;
    PglDevice& operator=(const PglDevice&) = delete;

    /**
     * @brief Initialize the device: allocate command buffers, configure SPI and I2C.
     * @return true on success.
     *
     * TODO(M2): Implement actual ESP32-S3 LCD peripheral + I2C initialization.
     *           Currently allocates the command buffers only.
     */
    bool Initialize(const PglDeviceConfig& config) {
        config_ = config;

        // Allocate double command buffers (ping-pong)
        for (int i = 0; i < 2; ++i) {
            cmdBuffers_[i] = AllocateBuffer(config_.commandBufferSize);
            if (!cmdBuffers_[i]) {
                Destroy();
                return false;
            }
        }

        activeBuffer_ = 0;
        encoder_ = new PglEncoder(cmdBuffers_[activeBuffer_], config_.commandBufferSize);
        initialized_ = true;
        return true;
    }

    void Destroy() {
        delete encoder_;
        encoder_ = nullptr;

        for (int i = 0; i < 2; ++i) {
            FreeBuffer(cmdBuffers_[i]);
            cmdBuffers_[i] = nullptr;
        }

        initialized_ = false;
    }

    bool IsInitialized() const { return initialized_; }

    /**
     * @brief Get the encoder for the current frame.
     *        Call between BeginFrame() and EndFrame().
     */
    PglEncoder* GetEncoder() { return encoder_; }

    // ─── Frame Lifecycle ────────────────────────────────────────────────

    /// Begin recording a new frame.
    void BeginFrame(uint32_t frameNumber, uint32_t frameTimeUs) {
        if (!initialized_ || !encoder_) return;

        // Switch to the inactive buffer for this frame
        activeBuffer_ = 1 - activeBuffer_;
        encoder_->~PglEncoder();
        new (encoder_) PglEncoder(cmdBuffers_[activeBuffer_], config_.commandBufferSize);

        encoder_->BeginFrame(frameNumber, frameTimeUs);
    }

    /// Finalize the frame and trigger DMA transfer.
    void EndFrame() {
        if (!initialized_ || !encoder_) return;

        encoder_->EndFrame();

        if (encoder_->HasOverflow()) {
            // TODO: Log/handle overflow — frame is too large for buffer
            return;
        }

        // TODO(M2): Wait for RDY pin high, then trigger LCD DMA transfer
        //           of encoder_->GetBuffer(), encoder_->GetLength() bytes.
        SubmitDMA(encoder_->GetBuffer(), encoder_->GetLength());
    }

    // ─── I2C Configuration ──────────────────────────────────────────────

    void SetBrightness(uint8_t brightness) {
        uint8_t data[] = { PGL_REG_SET_BRIGHTNESS, 1, brightness };
        WriteI2C(data, sizeof(data));
    }

    void SetPanelConfig(uint16_t width, uint16_t height) {
        uint8_t data[6] = { PGL_REG_SET_PANEL_CONFIG, 4 };
        std::memcpy(&data[2], &width, 2);
        std::memcpy(&data[4], &height, 2);
        WriteI2C(data, sizeof(data));
    }

    void SetScanRate(uint8_t scanRate) {
        uint8_t data[] = { PGL_REG_SET_SCAN_RATE, 1, scanRate };
        WriteI2C(data, sizeof(data));
    }

    void ClearDisplay() {
        uint8_t data[] = { PGL_REG_CLEAR_DISPLAY, 0 };
        WriteI2C(data, sizeof(data));
    }

    void SetGammaTable(uint8_t table) {
        uint8_t data[] = { PGL_REG_SET_GAMMA_TABLE, 1, table };
        WriteI2C(data, sizeof(data));
    }

    void ResetGPU() {
        uint8_t data[] = { PGL_REG_RESET_GPU, 0 };
        WriteI2C(data, sizeof(data));
    }

    PglStatusResponse QueryStatus() {
        PglStatusResponse status{};
        // TODO(M2): Implement actual I2C read from GPU
        // uint8_t reg = PGL_REG_STATUS_REQUEST;
        // WriteI2C(&reg, 1);
        // ReadI2C(&status, sizeof(status));
        return status;
    }

    /**
     * @brief Query GPU capabilities (architecture, core count, memory, limits).
     *        Useful for host-side adaptation to different GPU implementations
     *        (ARM Cortex-M33, RISC-V, FPGA, etc.).
     */
    PglCapabilityResponse QueryCapability() {
        PglCapabilityResponse cap{};
        // TODO(M2): Implement actual I2C read from GPU
        // uint8_t reg = PGL_REG_CAPABILITY_QUERY;
        // WriteI2C(&reg, 1);
        // ReadI2C(&cap, sizeof(cap));
        return cap;
    }

private:
    // ─── Transport Stubs (to be implemented in M2) ──────────────────────

    void SubmitDMA(const uint8_t* data, size_t length) {
        (void)data;
        (void)length;
        // TODO(M2): Configure ESP32-S3 LCD/Octal-SPI DMA descriptor chain
        //           pointing to `data` of `length` bytes and trigger transfer.
        //
        // Implementation sketch:
        //   esp_lcd_panel_io_tx_color(panel_io, -1, data, length);
        //
        // Or if using raw SPI:
        //   spi_device_queue_trans(spi_handle, &transaction, portMAX_DELAY);
    }

    void WriteI2C(const uint8_t* data, size_t length) {
        (void)data;
        (void)length;
        // TODO(M2): Wire.beginTransmission(config_.i2cAddress);
        //           Wire.write(data, length);
        //           Wire.endTransmission();
    }

    void ReadI2C(void* dest, size_t length) {
        (void)dest;
        (void)length;
        // TODO(M2): Wire.requestFrom(config_.i2cAddress, length);
        //           Wire.readBytes(dest, length);
    }

    uint8_t* AllocateBuffer(size_t size) {
        // Prefer PSRAM for the large command buffers (written sequentially — no
        // random access penalty). Fall back to internal heap.
        //
        // TODO: Use heap_caps_malloc on ESP32. For now, plain new.
        return new (std::nothrow) uint8_t[size];
    }

    void FreeBuffer(uint8_t* buffer) {
        delete[] buffer;
    }

    PglDeviceConfig config_{};
    uint8_t*    cmdBuffers_[2]  = { nullptr, nullptr };
    int         activeBuffer_   = 0;
    PglEncoder* encoder_        = nullptr;
    bool        initialized_    = false;
};
