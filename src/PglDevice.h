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
#include <new>

#ifdef ARDUINO_ARCH_ESP32
#include <Arduino.h>
#include <Wire.h>
#include <esp_lcd_panel_io.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#endif

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

    // I2C bus instance (0 or 1)
    uint8_t i2cPort        = 0;        // Wire (0) or Wire1 (1)

    // DMA timeout (milliseconds) — how long to wait for RDY before dropping frame
    uint16_t rdyTimeoutMs  = 5;
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

#ifdef ARDUINO_ARCH_ESP32
        // ── Configure I2C Master ────────────────────────────────────────
        TwoWire& wire = GetWire();
        if (config_.i2cSdaPin >= 0 && config_.i2cSclPin >= 0) {
            wire.begin(config_.i2cSdaPin, config_.i2cSclPin, 400000UL);
        }

        // ── Configure Octal SPI via ESP32-S3 LCD Parallel Mode ──────────
        if (config_.spiClkPin >= 0 && config_.spiCsPin >= 0) {
            if (!InitLcdParallel()) {
                Destroy();
                return false;
            }
        }

        // ── Configure RDY pin (input, active high) ──────────────────────
        if (config_.rdyPin >= 0) {
            gpio_config_t rdyCfg = {};
            rdyCfg.pin_bit_mask = (1ULL << config_.rdyPin);
            rdyCfg.mode         = GPIO_MODE_INPUT;
            rdyCfg.pull_up_en   = GPIO_PULLUP_DISABLE;
            rdyCfg.pull_down_en = GPIO_PULLDOWN_ENABLE;  // default low until GPU asserts
            rdyCfg.intr_type    = GPIO_INTR_DISABLE;
            gpio_config(&rdyCfg);
        }
#endif

        initialized_ = true;
        return true;
    }

    void Destroy() {
#ifdef ARDUINO_ARCH_ESP32
        if (panelIo_) {
            esp_lcd_panel_io_del(panelIo_);
            panelIo_ = nullptr;
        }
#endif
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
            overflowFrames_++;
            return;
        }

        // Wait for GPU to be ready, then transmit
        if (!WaitForReady()) {
            droppedFrames_++;
            return;
        }

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
        uint8_t reg = PGL_REG_STATUS_REQUEST;
        WriteI2C(&reg, 1);
        ReadI2C(&status, sizeof(status));
        return status;
    }

    /**
     * @brief Query GPU capabilities (architecture, core count, memory, limits).
     */
    PglCapabilityResponse QueryCapability() {
        PglCapabilityResponse cap{};
        uint8_t reg = PGL_REG_CAPABILITY_QUERY;
        WriteI2C(&reg, 1);
        ReadI2C(&cap, sizeof(cap));
        return cap;
    }

    // ─── Status ─────────────────────────────────────────────────────────

    uint32_t GetDroppedFrames() const { return droppedFrames_; }
    uint32_t GetOverflowFrames() const { return overflowFrames_; }

    /// Check if the GPU's RDY pin is currently asserted.
    bool IsGpuReady() const {
#ifdef ARDUINO_ARCH_ESP32
        if (config_.rdyPin >= 0) {
            return gpio_get_level(static_cast<gpio_num_t>(config_.rdyPin)) != 0;
        }
#endif
        return true;  // If no RDY pin configured, assume always ready
    }

private:
    // ─── Transport Implementation ───────────────────────────────────────

#ifdef ARDUINO_ARCH_ESP32
    TwoWire& GetWire() {
        return (config_.i2cPort == 1) ? Wire1 : Wire;
    }

    bool InitLcdParallel() {
        esp_lcd_i80_bus_handle_t bus = nullptr;
        esp_lcd_i80_bus_config_t bus_config = {};
        bus_config.clk_src        = LCD_CLK_SRC_DEFAULT;
        bus_config.dc_gpio_num    = -1;  // Not used — we don't need a D/C pin
        bus_config.wr_gpio_num    = config_.spiClkPin;  // CLK acts as write-clock
        bus_config.data_gpio_nums[0] = config_.spiDataPins[0];
        bus_config.data_gpio_nums[1] = config_.spiDataPins[1];
        bus_config.data_gpio_nums[2] = config_.spiDataPins[2];
        bus_config.data_gpio_nums[3] = config_.spiDataPins[3];
        bus_config.data_gpio_nums[4] = config_.spiDataPins[4];
        bus_config.data_gpio_nums[5] = config_.spiDataPins[5];
        bus_config.data_gpio_nums[6] = config_.spiDataPins[6];
        bus_config.data_gpio_nums[7] = config_.spiDataPins[7];
        bus_config.bus_width       = 8;
        bus_config.max_transfer_bytes = config_.commandBufferSize + 64;
        bus_config.psram_trans_align  = 64;
        bus_config.sram_trans_align   = 4;

        esp_err_t err = esp_lcd_new_i80_bus(&bus_config, &bus);
        if (err != ESP_OK) return false;

        esp_lcd_panel_io_i80_config_t io_config = {};
        io_config.cs_gpio_num         = config_.spiCsPin;
        io_config.pclk_hz             = static_cast<uint32_t>(config_.spiClockMHz) * 1000000u;
        io_config.trans_queue_depth    = 2;  // Double-buffered transfers
        io_config.lcd_cmd_bits         = 0;
        io_config.lcd_param_bits       = 8;
        io_config.dc_levels.dc_idle_level   = 0;
        io_config.dc_levels.dc_cmd_level    = 0;
        io_config.dc_levels.dc_dummy_level  = 0;
        io_config.dc_levels.dc_data_level   = 0;
        io_config.flags.cs_active_high = false;

        err = esp_lcd_new_panel_io_i80(bus, &io_config, &panelIo_);
        if (err != ESP_OK) {
            esp_lcd_del_i80_bus(bus);
            return false;
        }

        return true;
    }
#endif

    bool WaitForReady() {
#ifdef ARDUINO_ARCH_ESP32
        if (config_.rdyPin < 0) return true;

        const uint32_t startMs = millis();
        while (!IsGpuReady()) {
            if ((millis() - startMs) >= config_.rdyTimeoutMs) {
                return false;  // GPU not ready — drop this frame
            }
            delayMicroseconds(10);
        }
#endif
        return true;
    }

    void SubmitDMA(const uint8_t* data, size_t length) {
#ifdef ARDUINO_ARCH_ESP32
        if (panelIo_) {
            // Use LCD panel IO to DMA the command buffer to the GPU.
            // esp_lcd_panel_io_tx_param sends data over the 8-bit parallel bus.
            // The RP2350 PIO receiver sees this as Octal SPI with CS framing.
            esp_lcd_panel_io_tx_param(panelIo_, -1, data, length);
        }
#else
        (void)data;
        (void)length;
#endif
    }

    void WriteI2C(const uint8_t* data, size_t length) {
#ifdef ARDUINO_ARCH_ESP32
        TwoWire& wire = GetWire();
        wire.beginTransmission(config_.i2cAddress);
        wire.write(data, length);
        wire.endTransmission();
#else
        (void)data;
        (void)length;
#endif
    }

    void ReadI2C(void* dest, size_t length) {
#ifdef ARDUINO_ARCH_ESP32
        TwoWire& wire = GetWire();
        wire.requestFrom(config_.i2cAddress, static_cast<size_t>(length));
        uint8_t* d = static_cast<uint8_t*>(dest);
        size_t read = 0;
        while (wire.available() && read < length) {
            d[read++] = wire.read();
        }
        // Zero-fill if fewer bytes received
        while (read < length) {
            d[read++] = 0;
        }
#else
        (void)dest;
        std::memset(dest, 0, length);
#endif
    }

    uint8_t* AllocateBuffer(size_t size) {
#ifdef ARDUINO_ARCH_ESP32
        // Prefer PSRAM for the large command buffers (written sequentially — no
        // random-access penalty).  DMA reads are sequential too.
        // Align to 64 bytes for PSRAM DMA alignment requirements.
        uint8_t* buf = static_cast<uint8_t*>(
            heap_caps_aligned_alloc(64, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (buf) return buf;

        // Fall back to internal SRAM
        buf = static_cast<uint8_t*>(
            heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        if (buf) return buf;
#endif
        return new (std::nothrow) uint8_t[size];
    }

    void FreeBuffer(uint8_t* buffer) {
#ifdef ARDUINO_ARCH_ESP32
        heap_caps_free(buffer);
#else
        delete[] buffer;
#endif
    }

    PglDeviceConfig config_{};
    uint8_t*    cmdBuffers_[2]  = { nullptr, nullptr };
    int         activeBuffer_   = 0;
    PglEncoder* encoder_        = nullptr;
    bool        initialized_    = false;
    uint32_t    droppedFrames_  = 0;
    uint32_t    overflowFrames_ = 0;

#ifdef ARDUINO_ARCH_ESP32
    esp_lcd_panel_io_handle_t panelIo_ = nullptr;
#endif
};
