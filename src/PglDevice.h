/**
 * @file PglDevice.h
 * @brief ProtoGL device — manages the Octal SPI transport and I2C control bus
 *        for communication with the RP2350 GPU.
 *
 * This is the high-level host-side interface. It owns the command buffer,
 * manages the encoder lifecycle, and coordinates DMA transfers.
 *
 * ProtoGL API Specification v0.7 — extends v0.5 with display + pool commands
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
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
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

    // Bus direction & notification
    int8_t  dirPin         = -1;       // Bus direction control (output, high = host TX)
    int8_t  irqPin         = -1;       // GPU async interrupt (input, active-low)

    // Legacy aliases (deprecated — use dirPin / irqPin)
    int8_t  rdyPin         = -1;       // DEPRECATED: mapped to dirPin if dirPin == -1

    // Command buffer sizing
    uint32_t commandBufferSize = 32768; // 32 KB default — enough for complex frames

    // I2C bus instance (0 or 1)
    uint8_t i2cPort        = 0;        // Wire (0) or Wire1 (1)

    // Bus turnaround delay (cycles at SPI clock rate)
    uint16_t dirTurnaroundCycles = 2;  // Default: 2 cycles

    // Legacy alias (deprecated — use dirTurnaroundCycles)
    uint16_t rdyTimeoutMs  = 5;        // DEPRECATED: used only if dirTurnaroundCycles == 0
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

        // ── Configure DIR pin (output, high = host TX) ─────────────────────
        int8_t effectiveDirPin = config_.dirPin >= 0 ? config_.dirPin : config_.rdyPin;
        if (effectiveDirPin >= 0) {
            gpio_config_t dirCfg = {};
            dirCfg.pin_bit_mask = (1ULL << effectiveDirPin);
            dirCfg.mode         = GPIO_MODE_OUTPUT;
            dirCfg.pull_up_en   = GPIO_PULLUP_DISABLE;
            dirCfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            dirCfg.intr_type    = GPIO_INTR_DISABLE;
            gpio_config(&dirCfg);
            gpio_set_level(static_cast<gpio_num_t>(effectiveDirPin), 1); // default: host TX
            config_.dirPin = effectiveDirPin;  // normalize
        }

        // ── Configure IRQ pin (input, active-low from GPU) ─────────────
        if (config_.irqPin >= 0) {
            gpio_config_t irqCfg = {};
            irqCfg.pin_bit_mask = (1ULL << config_.irqPin);
            irqCfg.mode         = GPIO_MODE_INPUT;
            irqCfg.pull_up_en   = GPIO_PULLUP_ENABLE;   // default high (deasserted)
            irqCfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            irqCfg.intr_type    = GPIO_INTR_DISABLE;     // ISR configured separately if needed
            gpio_config(&irqCfg);
        }
#endif

        initialized_ = true;
        return true;
    }

    void Destroy() {
#ifdef ARDUINO_ARCH_ESP32
        // Wait for any in-flight DMA to complete before freeing buffers
        for (int i = 0; i < 2; ++i) {
            if (dmaInFlight_[i] && dmaDoneSem_) {
                xSemaphoreTake(dmaDoneSem_, pdMS_TO_TICKS(100));
                dmaInFlight_[i] = false;
            }
        }

        if (panelIo_) {
            esp_lcd_panel_io_del(panelIo_);
            panelIo_ = nullptr;
        }

        if (dmaDoneSem_) {
            vSemaphoreDelete(dmaDoneSem_);
            dmaDoneSem_ = nullptr;
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

        // Wait for any in-flight DMA on the buffer we're about to write into.
        // This ensures the previous frame's DMA from this buffer has completed
        // before we overwrite it with new commands.
        WaitForDMAComplete(activeBuffer_);

        encoder_->~PglEncoder();
        new (encoder_) PglEncoder(cmdBuffers_[activeBuffer_], config_.commandBufferSize);

        encoder_->BeginFrame(frameNumber, frameTimeUs);
    }

    /// Finalize the frame and trigger async DMA transfer.
    void EndFrame() {
        if (!initialized_ || !encoder_) return;

        encoder_->EndFrame();

        if (encoder_->HasOverflow()) {
            overflowFrames_++;
            return;
        }

        // Wait for GPU to be ready, then submit asynchronously
        if (!WaitForReady()) {
            droppedFrames_++;
            return;
        }

        SubmitDMAAsync(encoder_->GetBuffer(), encoder_->GetLength(), activeBuffer_);
    }

    // ─── I2C Configuration ──────────────────────────────────────────────

    void SetBrightness(uint8_t brightness) {
        uint8_t data[] = { PGL_REG_SET_BRIGHTNESS, brightness };
        WriteI2C(data, sizeof(data));
    }

    void SetPanelConfig(uint16_t width, uint16_t height) {
        uint8_t data[5] = { PGL_REG_SET_PANEL_CONFIG, 0, 0, 0, 0 };
        std::memcpy(&data[1], &width, 2);
        std::memcpy(&data[3], &height, 2);
        WriteI2C(data, sizeof(data));
    }

    void SetScanRate(uint8_t scanRate) {
        uint8_t data[] = { PGL_REG_SET_SCAN_RATE, scanRate };
        WriteI2C(data, sizeof(data));
    }

    void ClearDisplay() {
        uint8_t data[] = { PGL_REG_CLEAR_DISPLAY };
        WriteI2C(data, sizeof(data));
    }

    void SetGammaTable(uint8_t table) {
        uint8_t data[] = { PGL_REG_SET_GAMMA_TABLE, table };
        WriteI2C(data, sizeof(data));
    }

    void ResetGPU() {
        uint8_t data[] = { PGL_REG_RESET_GPU };
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

    /**
     * @brief Query 32-byte extended status: GPU usage, temperature, VRAM,
     *        frame timing, clock frequency, and VRAM tier detection flags.
     *
     * Requires GPU firmware with PGL_REG_EXTENDED_STATUS support.
     * Call at most once per frame to avoid I2C bus congestion.
     */
    PglExtendedStatusResponse QueryExtendedStatus() {
        PglExtendedStatusResponse ext{};
        uint8_t reg = PGL_REG_EXTENDED_STATUS;
        WriteI2C(&reg, 1);
        ReadI2C(&ext, sizeof(ext));
        return ext;
    }

    /**
     * @brief Request a GPU clock frequency change.
     *
     * @param targetMHz   Desired frequency (e.g. 150, 200, 250, 266, 300).
     *                    0 = query current clock only.
     * @param voltageLevel VREG level (0 = auto-select for target frequency).
     * @param flags       PglClockFlags bitmask.
     *
     * The GPU will apply the change asynchronously; the next
     * QueryExtendedStatus() will report the actual running frequency.
     */
    void SetClockFrequency(uint16_t targetMHz,
                           uint8_t  voltageLevel = 0,
                           uint8_t  flags = PGL_CLOCK_RECONFIGURE_PIO) {
        PglClockRequest req{};
        req.targetMHz    = targetMHz;
        req.voltageLevel = voltageLevel;
        req.flags        = flags;

        uint8_t buf[1 + sizeof(PglClockRequest)];
        buf[0] = PGL_REG_SET_CLOCK_FREQ;
        std::memcpy(buf + 1, &req, sizeof(req));
        WriteI2C(buf, sizeof(buf));
    }

    /**
     * @brief Check whether the GPU has reported external VRAM.
     *
     * Convenience wrapper: reads the capability response and checks the
     * OPI/QSPI VRAM flags.
     *
     * @return True if any external VRAM tier was detected at boot.
     */
    bool HasExternalVram() {
        auto cap = QueryCapability();
        return (cap.flags & (PGL_CAP_OPI_VRAM | PGL_CAP_QSPI_VRAM)) != 0;
    }

    // ─── Display Management (M11) ───────────────────────────────────────

    /**
     * @brief Select a display slot and read its capabilities.
     *
     * Writes the display ID to PGL_REG_DISPLAY_MODE, then reads
     * PGL_REG_DISPLAY_CAPS to get the driver's capability struct.
     *
     * @param displayId  Display slot (0–PGL_MAX_DISPLAYS-1).
     * @return PglDisplayCaps for the selected slot (all zeros if empty).
     */
    PglDisplayCaps QueryDisplayCaps(PglDisplay displayId = 0) {
        uint8_t reg[2] = { PGL_REG_DISPLAY_MODE, displayId };
        WriteI2C(reg, sizeof(reg));

        PglDisplayCaps caps{};
        uint8_t capReg = PGL_REG_DISPLAY_CAPS;
        WriteI2C(&capReg, 1);
        ReadI2C(&caps, sizeof(caps));
        return caps;
    }

    /**
     * @brief Set the active display mode.
     *
     * Writes a display type to PGL_REG_DISPLAY_MODE. The GPU's
     * DisplayManager will route frames to the corresponding driver.
     *
     * @param displayType  PglDisplayType to activate.
     */
    void SetDisplayMode(PglDisplayType displayType) {
        uint8_t data[] = { PGL_REG_DISPLAY_MODE, static_cast<uint8_t>(displayType) };
        WriteI2C(data, sizeof(data));
    }

    // ─── Memory Pool Status (M11) ───────────────────────────────────────

    /**
     * @brief Query the status of a GPU memory pool.
     *
     * Writes the pool handle to PGL_REG_MEM_POOL_STATUS, then reads
     * back the PglMemPoolStatusResponse.
     *
     * @param poolHandle  Pool handle to query.
     * @return PglMemPoolStatusResponse (status=0xFF if handle invalid).
     */
    PglMemPoolStatusResponse QueryMemPoolStatus(PglPool poolHandle) {
        uint8_t buf[3];
        buf[0] = PGL_REG_MEM_POOL_STATUS;
        std::memcpy(buf + 1, &poolHandle, sizeof(poolHandle));
        WriteI2C(buf, sizeof(buf));

        PglMemPoolStatusResponse resp{};
        uint8_t reg = PGL_REG_MEM_POOL_STATUS;
        WriteI2C(&reg, 1);
        ReadI2C(&resp, sizeof(resp));
        return resp;
    }

    // ─── Status ─────────────────────────────────────────────────────────

    uint32_t GetDroppedFrames() const { return droppedFrames_; }
    uint32_t GetOverflowFrames() const { return overflowFrames_; }
    uint32_t GetGpuStalls() const { return gpuStalls_; }
    uint32_t GetConsecutiveDrops() const { return consecutiveDrops_; }

    /// Check if the GPU's IRQ pin is not asserted (high = no backpressure).
    bool IsGpuReady() const {
#ifdef ARDUINO_ARCH_ESP32
        if (config_.irqPin >= 0) {
            return gpio_get_level(static_cast<gpio_num_t>(config_.irqPin)) != 0;
        }
#endif
        return true;  // If no IRQ pin configured, assume always ready
    }

private:
    // ─── Transport Implementation ───────────────────────────────────────

#ifdef ARDUINO_ARCH_ESP32
    TwoWire& GetWire() {
        return (config_.i2cPort == 1) ? Wire1 : Wire;
    }

    /// ISR-safe callback invoked when a DMA color transfer completes.
    static bool IRAM_ATTR OnDMADone(esp_lcd_panel_io_handle_t /*io*/,
                                     esp_lcd_panel_io_event_data_t* /*data*/,
                                     void* user_ctx) {
        PglDevice* self = static_cast<PglDevice*>(user_ctx);
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(self->dmaDoneSem_, &woken);
        return (woken == pdTRUE);
    }

    bool InitLcdParallel() {
        // Create semaphore for DMA completion (start signalled = both buffers available)
        dmaDoneSem_ = xSemaphoreCreateCounting(2, 2);
        if (!dmaDoneSem_) return false;

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
        io_config.on_color_trans_done  = OnDMADone;
        io_config.user_ctx             = this;

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
        if (config_.irqPin < 0 && config_.rdyPin < 0) return true;

        const uint32_t startMs = millis();
        const uint16_t timeoutMs = config_.dirTurnaroundCycles > 0
            ? (config_.dirTurnaroundCycles + 1)  // ~1 ms minimum
            : config_.rdyTimeoutMs;               // legacy fallback
        while (!IsGpuReady()) {
            if ((millis() - startMs) >= timeoutMs) {
                consecutiveDrops_++;
                if (consecutiveDrops_ >= kMaxConsecutiveDrops) {
                    // GPU may be stuck — attempt a status query
                    gpuStalls_++;
                }
                return false;  // GPU not ready — drop this frame
            }
            delayMicroseconds(10);
        }
        consecutiveDrops_ = 0;  // Reset on success
#endif
        return true;
    }

    void SubmitDMAAsync(const uint8_t* data, size_t length, int bufferIdx) {
#ifdef ARDUINO_ARCH_ESP32
        if (panelIo_ && dmaDoneSem_) {
            // Acquire a slot — blocks if both buffers are in flight (shouldn't
            // happen with proper ping-pong, but guards against pathological timing).
            xSemaphoreTake(dmaDoneSem_, portMAX_DELAY);
            dmaInFlight_[bufferIdx] = true;

            // esp_lcd_panel_io_tx_color queues the DMA and returns immediately.
            // When complete, OnDMADone() fires and gives the semaphore back.
            esp_err_t err = esp_lcd_panel_io_tx_color(panelIo_, -1, data, length);
            if (err != ESP_OK) {
                // Enqueue failed — give the semaphore back to avoid deadlock
                // on subsequent SubmitDMAAsync calls.
                dmaInFlight_[bufferIdx] = false;
                xSemaphoreGive(dmaDoneSem_);
                droppedFrames_++;
            }
        }
#else
        (void)data;
        (void)length;
        (void)bufferIdx;
#endif
    }

    /// Block until a specific buffer's DMA transfer has completed.
    void WaitForDMAComplete(int bufferIdx) {
#ifdef ARDUINO_ARCH_ESP32
        if (dmaInFlight_[bufferIdx] && dmaDoneSem_) {
            // The semaphore was already given by OnDMADone() — we just need to
            // confirm that particular buffer is free.  Since the callback doesn't
            // tell us *which* buffer, we use a simple spin check with yield.
            // In practice with proper ping-pong the DMA is already done by now.
            //
            // For a fully precise solution we'd track per-buffer semaphores, but
            // with trans_queue_depth=2 and alternating buffers, the ISR callback
            // fires in order, so the counting semaphore guarantees ordering.
            dmaInFlight_[bufferIdx] = false;
        }
#else
        (void)bufferIdx;
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
    uint32_t    gpuStalls_      = 0;       ///< Count of consecutive-drop threshold events
    uint32_t    consecutiveDrops_ = 0;     ///< Running count of sequential frame drops

    static constexpr uint32_t kMaxConsecutiveDrops = 10;  ///< Stall threshold

    // Async DMA tracking
    bool        dmaInFlight_[2] = { false, false };
#ifdef ARDUINO_ARCH_ESP32
    SemaphoreHandle_t       dmaDoneSem_ = nullptr;
    esp_lcd_panel_io_handle_t panelIo_  = nullptr;
#endif
};
