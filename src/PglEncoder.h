/**
 * @file PglEncoder.h
 * @brief ProtoGL command buffer encoder (ESP32-S3 host side).
 *
 * Records ProtoGL commands into a linear byte buffer suitable for
 * DMA transfer over Octal SPI to the RP2350 GPU.
 *
 * Usage:
 *   PglEncoder encoder(bufferPtr, bufferSize);
 *   encoder.BeginFrame(frameNum, deltaTimeUs);
 *   encoder.SetCamera(...);
 *   encoder.DrawObject(...);
 *   encoder.EndFrame();
 *   // encoder.GetBuffer() / encoder.GetLength() → DMA source
 *
 * ProtoGL API Specification v0.7.2 — extends v0.7 with 2D layers, defrag, persistence
 */

#pragma once

#include "PglTypes.h"
#include "PglOpcodes.h"
#include "PglCRC16.h"
#include <cstring>

class PglEncoder {
public:
    /**
     * @brief Construct an encoder writing into an externally-owned buffer.
     * @param buffer   Pointer to DMA-capable byte buffer (PSRAM or internal).
     * @param capacity Total capacity in bytes.
     */
    PglEncoder(uint8_t* buffer, size_t capacity)
        : buffer_(buffer), capacity_(capacity) {
        Reset();
    }

    /// Reset encoder state for a new frame (does not zero the buffer).
    void Reset() {
        writePos_ = 0;
        commandCount_ = 0;
        overflow_ = false;
    }

    /// Returns true if any write exceeded the buffer capacity.
    bool HasOverflow() const { return overflow_; }

    /// Current number of bytes written.
    size_t GetLength() const { return writePos_; }

    /// Pointer to the buffer start (for DMA).
    const uint8_t* GetBuffer() const { return buffer_; }
    uint8_t* GetBuffer() { return buffer_; }

    /// Number of commands recorded (between BeginFrame and EndFrame).
    uint16_t GetCommandCount() const { return commandCount_; }

    // ─── Frame Commands ─────────────────────────────────────────────────

    /**
     * @brief Begin a new frame. Writes the frame header (placeholder values for
     *        totalLength and commandCount, patched in EndFrame).
     */
    void BeginFrame(uint32_t frameNumber, uint32_t frameTimeUs) {
        Reset();
        frameHeaderPos_ = writePos_;
        currentFrameNumber_ = frameNumber;

        // Write frame header with placeholders
        PglFrameHeader hdr{};
        hdr.syncWord     = PGL_SYNC_WORD;
        hdr.frameNumber  = frameNumber;
        hdr.totalLength  = 0;  // patched in EndFrame
        hdr.commandCount = 0;  // patched in EndFrame
        WriteRaw(&hdr, sizeof(hdr));

        // Write CMD_BEGIN_FRAME
        PglCmdBeginFrame payload{};
        payload.frameNumber = frameNumber;
        payload.frameTimeUs = frameTimeUs;
        WriteCommand(PGL_CMD_BEGIN_FRAME, &payload, sizeof(payload));
    }

    /**
     * @brief End the current frame. Patches the frame header, appends CRC-16.
     */
    void EndFrame() {
        // Write CMD_END_FRAME
        PglCmdEndFrame payload{};
        payload.frameNumber = currentFrameNumber_;
        WriteCommand(PGL_CMD_END_FRAME, &payload, sizeof(payload));

        // Patch frame header
        if (!overflow_ && writePos_ + sizeof(PglFrameFooter) <= capacity_) {
            const uint32_t totalLen = static_cast<uint32_t>(writePos_ + sizeof(PglFrameFooter));
            PglFrameHeader* hdr = reinterpret_cast<PglFrameHeader*>(buffer_ + frameHeaderPos_);
            hdr->totalLength  = totalLen;
            hdr->commandCount = commandCount_;

            // Compute and append CRC-16 over everything so far
            uint16_t crc = PglCRC16::Compute(buffer_, writePos_);
            PglFrameFooter footer{};
            footer.crc16 = crc;
            WriteRaw(&footer, sizeof(footer));
        }
    }

    // ─── Camera ─────────────────────────────────────────────────────────

    void SetCamera(PglCamera cameraId, PglLayout layoutId,
                   const PglVec3& position, const PglQuat& rotation,
                   const PglVec3& scale, const PglQuat& lookOffset,
                   const PglQuat& baseRotation, bool is2D) {
        PglCmdSetCamera payload{};
        payload.cameraId      = cameraId;
        payload.pixelLayoutId = layoutId;
        payload.position      = position;
        payload.rotation      = rotation;
        payload.scale         = scale;
        payload.lookOffset    = lookOffset;
        payload.baseRotation  = baseRotation;
        payload.is2D          = is2D ? 1 : 0;
        WriteCommand(PGL_CMD_SET_CAMERA, &payload, sizeof(payload));
    }

    // ─── Draw Calls ─────────────────────────────────────────────────────

    void DrawObject(PglMesh meshId, PglMaterial materialId,
                    const PglVec3& position, const PglQuat& rotation,
                    const PglVec3& scale,
                    const PglQuat& baseRotation,
                    const PglQuat& scaleRotationOffset,
                    const PglVec3& scaleOffset,
                    const PglVec3& rotationOffset,
                    bool enabled) {
        PglCmdDrawObject payload{};
        payload.meshId               = meshId;
        payload.materialId           = materialId;
        payload.flags                = enabled ? PGL_DRAW_ENABLED : 0;
        payload.position             = position;
        payload.rotation             = rotation;
        payload.scale                = scale;
        payload.baseRotation         = baseRotation;
        payload.scaleRotationOffset  = scaleRotationOffset;
        payload.scaleOffset          = scaleOffset;
        payload.rotationOffset       = rotationOffset;
        WriteCommand(PGL_CMD_DRAW_OBJECT, &payload, sizeof(payload));
    }

    void DrawObjectMorphed(PglMesh meshId, PglMaterial materialId,
                           const PglVec3& position, const PglQuat& rotation,
                           const PglVec3& scale,
                           const PglQuat& baseRotation,
                           const PglQuat& scaleRotationOffset,
                           const PglVec3& scaleOffset,
                           const PglVec3& rotationOffset,
                           bool enabled,
                           const PglVec3* vertices, uint16_t vertexCount) {
        PglCmdDrawObject payload{};
        payload.meshId               = meshId;
        payload.materialId           = materialId;
        payload.flags                = (enabled ? PGL_DRAW_ENABLED : 0) | PGL_DRAW_VERTEX_OVERRIDE;
        payload.position             = position;
        payload.rotation             = rotation;
        payload.scale                = scale;
        payload.baseRotation         = baseRotation;
        payload.scaleRotationOffset  = scaleRotationOffset;
        payload.scaleOffset          = scaleOffset;
        payload.rotationOffset       = rotationOffset;

        const uint16_t vertDataSize = vertexCount * sizeof(PglVec3);
        const uint16_t totalPayload = sizeof(payload) + sizeof(uint16_t) + vertDataSize;

        WriteCommandHeader(PGL_CMD_DRAW_OBJECT, totalPayload);
        WriteRaw(&payload, sizeof(payload));
        WriteRaw(&vertexCount, sizeof(vertexCount));
        WriteRaw(vertices, vertDataSize);
    }

    // ─── [SHADER:FUTURE] Shaders (Screen-Space Post-Processing) ──────
    // These encode methods are complete and ready to use.
    // GPUDriverController integration pending Effect RTTI — see [SHADER:FUTURE] block.

    /// Generic SetShader — caller fills params[] manually.
    void SetShader(PglCamera cameraId, uint8_t shaderSlot,
                   uint8_t shaderClass, float intensity,
                   const void* params = nullptr, uint8_t paramSize = 0) {
        PglCmdSetShader payload{};
        payload.cameraId   = cameraId;
        payload.shaderSlot = shaderSlot;
        payload.shaderClass = shaderClass;
        payload.intensity  = intensity;
        if (params && paramSize > 0) {
            if (paramSize > sizeof(payload.params)) paramSize = sizeof(payload.params);
            std::memcpy(payload.params, params, paramSize);
        }
        WriteCommand(PGL_CMD_SET_SHADER, &payload, sizeof(payload));
    }

    /// Clear a shader slot (sets class to PGL_SHADER_NONE).
    void ClearShader(PglCamera cameraId, uint8_t shaderSlot) {
        SetShader(cameraId, shaderSlot, PGL_SHADER_NONE, 0.0f);
    }

    // ── Convolution Shader Helpers ──────────────────────────────────────

    /// Convolution shader — fully parameterised.
    void SetConvolution(PglCamera cameraId, uint8_t slot, float intensity,
                        PglKernelShape kernel, uint8_t radius,
                        bool separable, float angleDeg,
                        float anglePeriod = 0.0f, float sigma = 0.0f) {
        PglShaderParamsConvolution p{};
        p.kernelShape  = kernel;
        p.radius       = radius;
        p.separable    = separable ? 1 : 0;
        p.angle        = angleDeg;
        p.anglePeriod  = anglePeriod;
        p.sigma        = sigma;
        SetShader(cameraId, slot, PGL_SHADER_CONVOLUTION, intensity, &p, sizeof(p));
    }

    /// Convenience: Horizontal blur (angle=0°, box kernel).
    void SetHorizontalBlur(PglCamera cameraId, uint8_t slot, float intensity, uint8_t radius) {
        SetConvolution(cameraId, slot, intensity, PGL_KERNEL_BOX, radius, false, 0.0f);
    }

    /// Convenience: Vertical blur (angle=90°, box kernel).
    void SetVerticalBlur(PglCamera cameraId, uint8_t slot, float intensity, uint8_t radius) {
        SetConvolution(cameraId, slot, intensity, PGL_KERNEL_BOX, radius, false, 90.0f);
    }

    /// Convenience: Radial blur (auto-rotating angle, box kernel).
    void SetRadialBlur(PglCamera cameraId, uint8_t slot, float intensity,
                       uint8_t radius, float rotationPeriod = 3.7f) {
        SetConvolution(cameraId, slot, intensity, PGL_KERNEL_BOX, radius, false,
                       0.0f, rotationPeriod);
    }

    /// Convenience: Anti-aliasing (separable 2D, 4-neighbour smoothing).
    void SetAntiAliasing(PglCamera cameraId, uint8_t slot, float intensity,
                         float smoothing = 0.25f) {
        SetConvolution(cameraId, slot, intensity, PGL_KERNEL_BOX, 1, true,
                       0.0f, 0.0f, smoothing);
    }

    // ── Displacement Shader Helpers ─────────────────────────────────────

    /// Displacement shader — fully parameterised.
    void SetDisplacement(PglCamera cameraId, uint8_t slot, float intensity,
                         PglDisplacementAxis axis, bool perChannel,
                         uint8_t amplitude, PglWaveform waveform,
                         float period, float frequency = 1.0f,
                         float phase1Period = 0.0f, float phase2Period = 0.0f) {
        PglShaderParamsDisplacement p{};
        p.axis        = axis;
        p.perChannel  = perChannel ? 1 : 0;
        p.amplitude   = amplitude;
        p.waveform    = waveform;
        p.period      = period;
        p.frequency   = frequency;
        p.phase1Period = phase1Period;
        p.phase2Period = phase2Period;
        SetShader(cameraId, slot, PGL_SHADER_DISPLACEMENT, intensity, &p, sizeof(p));
    }

    /// Convenience: Horizontal chromatic aberration (PhaseOffsetX).
    void SetPhaseOffsetX(PglCamera cameraId, uint8_t slot, float intensity,
                         uint8_t amplitude, float period = 3.5f) {
        SetDisplacement(cameraId, slot, intensity, PGL_AXIS_X, true,
                        amplitude, PGL_WAVE_SINE, period);
    }

    /// Convenience: Vertical chromatic aberration (PhaseOffsetY).
    void SetPhaseOffsetY(PglCamera cameraId, uint8_t slot, float intensity,
                         uint8_t amplitude, float period = 3.5f) {
        SetDisplacement(cameraId, slot, intensity, PGL_AXIS_Y, true,
                        amplitude, PGL_WAVE_SINE, period);
    }

    /// Convenience: Radial chromatic aberration (PhaseOffsetR).
    void SetPhaseOffsetR(PglCamera cameraId, uint8_t slot, float intensity,
                         uint8_t amplitude, float rotPeriod = 3.7f,
                         float phase1Period = 4.5f, float phase2Period = 3.2f) {
        SetDisplacement(cameraId, slot, intensity, PGL_AXIS_RADIAL, true,
                        amplitude, PGL_WAVE_SINE, rotPeriod, 1.0f,
                        phase1Period, phase2Period);
    }

    // ── Color Adjust Shader Helpers ─────────────────────────────────────

    /// Color adjust shader — fully parameterised.
    void SetColorAdjust(PglCamera cameraId, uint8_t slot, float intensity,
                        PglColorAdjustOp operation, float strength,
                        float param2 = 0.0f) {
        PglShaderParamsColorAdjust p{};
        p.operation = operation;
        p.strength  = strength;
        p.param2    = param2;
        SetShader(cameraId, slot, PGL_SHADER_COLOR_ADJUST, intensity, &p, sizeof(p));
    }

    /// Convenience: Edge feathering (dims edges adjacent to black pixels).
    void SetEdgeFeather(PglCamera cameraId, uint8_t slot, float intensity,
                        float featherStrength = 0.5f) {
        SetColorAdjust(cameraId, slot, intensity, PGL_COLOR_EDGE_FEATHER, featherStrength);
    }

    /// Convenience: Brightness adjustment.
    void SetBrightness(PglCamera cameraId, uint8_t slot, float intensity,
                       float brightness) {
        SetColorAdjust(cameraId, slot, intensity, PGL_COLOR_BRIGHTNESS, brightness);
    }

    /// Convenience: Contrast adjustment.
    void SetContrast(PglCamera cameraId, uint8_t slot, float intensity,
                     float contrast) {
        SetColorAdjust(cameraId, slot, intensity, PGL_COLOR_CONTRAST, contrast);
    }

    /// Convenience: Gamma correction.
    void SetGamma(PglCamera cameraId, uint8_t slot, float intensity,
                  float gammaExponent = 2.2f) {
        SetColorAdjust(cameraId, slot, intensity, PGL_COLOR_GAMMA, 1.0f, gammaExponent);
    }

    // ─── Programmable Shader Programs (v0.6) ────────────────────────────

    /**
     * @brief Upload a compiled shader program (PSB bytecode) to the GPU.
     * @param programId     GPU-side handle (0–PGL_MAX_SHADER_PROGRAMS-1).
     * @param bytecodeBlob  Pointer to the complete PSB binary (header + descriptors + constants + instructions).
     * @param bytecodeSize  Size of the PSB binary in bytes.
     */
    void CreateShaderProgram(uint16_t programId,
                             const void* bytecodeBlob, uint16_t bytecodeSize) {
        PglCmdCreateShaderProgramHeader hdr{};
        hdr.programId    = programId;
        hdr.bytecodeSize = bytecodeSize;

        const uint16_t totalPayload = sizeof(hdr) + bytecodeSize;
        WriteCommandHeader(PGL_CMD_CREATE_SHADER_PROGRAM, totalPayload);
        WriteRaw(&hdr, sizeof(hdr));
        WriteRaw(bytecodeBlob, bytecodeSize);
    }

    /**
     * @brief Destroy a previously uploaded shader program.
     * @param programId  GPU-side handle to destroy.
     */
    void DestroyShaderProgram(uint16_t programId) {
        PglCmdDestroyShaderProgram payload{};
        payload.programId = programId;
        WriteCommand(PGL_CMD_DESTROY_SHADER_PROGRAM, &payload, sizeof(payload));
    }

    /**
     * @brief Bind a compiled shader program to a camera's shader slot.
     *
     * This replaces the old SetShader() for programmable effects. The existing
     * SetShader()/SetConvolution()/etc. still work for built-in shader classes.
     *
     * @param cameraId   Target camera.
     * @param shaderSlot Slot index (0–3).
     * @param programId  Program handle (0xFFFF = unbind / clear slot).
     * @param intensity  Global mix factor (0.0 = bypass, 1.0 = full effect).
     */
    void BindShaderProgram(PglCamera cameraId, uint8_t shaderSlot,
                           uint16_t programId, float intensity) {
        PglCmdBindShaderProgram payload{};
        payload.cameraId   = cameraId;
        payload.shaderSlot = shaderSlot;
        payload.programId  = programId;
        payload.intensity  = intensity;
        WriteCommand(PGL_CMD_BIND_SHADER_PROGRAM, &payload, sizeof(payload));
    }

    /// Set a float uniform on a loaded shader program.
    void SetShaderUniform(uint16_t programId, uint8_t uniformSlot,
                          float value) {
        PglCmdSetShaderUniformHeader hdr{};
        hdr.programId      = programId;
        hdr.uniformSlot    = uniformSlot;
        hdr.componentCount = 1;
        const uint16_t totalPayload = sizeof(hdr) + sizeof(float);
        WriteCommandHeader(PGL_CMD_SET_SHADER_UNIFORM, totalPayload);
        WriteRaw(&hdr, sizeof(hdr));
        WriteRaw(&value, sizeof(float));
    }

    /// Set a vec2 uniform on a loaded shader program.
    void SetShaderUniform(uint16_t programId, uint8_t uniformSlot,
                          float x, float y) {
        PglCmdSetShaderUniformHeader hdr{};
        hdr.programId      = programId;
        hdr.uniformSlot    = uniformSlot;
        hdr.componentCount = 2;
        float vals[2] = {x, y};
        const uint16_t totalPayload = sizeof(hdr) + 2 * sizeof(float);
        WriteCommandHeader(PGL_CMD_SET_SHADER_UNIFORM, totalPayload);
        WriteRaw(&hdr, sizeof(hdr));
        WriteRaw(vals, sizeof(vals));
    }

    /// Set a vec3 uniform on a loaded shader program.
    void SetShaderUniform(uint16_t programId, uint8_t uniformSlot,
                          float x, float y, float z) {
        PglCmdSetShaderUniformHeader hdr{};
        hdr.programId      = programId;
        hdr.uniformSlot    = uniformSlot;
        hdr.componentCount = 3;
        float vals[3] = {x, y, z};
        const uint16_t totalPayload = sizeof(hdr) + 3 * sizeof(float);
        WriteCommandHeader(PGL_CMD_SET_SHADER_UNIFORM, totalPayload);
        WriteRaw(&hdr, sizeof(hdr));
        WriteRaw(vals, sizeof(vals));
    }

    /// Set a vec4 uniform on a loaded shader program.
    void SetShaderUniform(uint16_t programId, uint8_t uniformSlot,
                          float x, float y, float z, float w) {
        PglCmdSetShaderUniformHeader hdr{};
        hdr.programId      = programId;
        hdr.uniformSlot    = uniformSlot;
        hdr.componentCount = 4;
        float vals[4] = {x, y, z, w};
        const uint16_t totalPayload = sizeof(hdr) + 4 * sizeof(float);
        WriteCommandHeader(PGL_CMD_SET_SHADER_UNIFORM, totalPayload);
        WriteRaw(&hdr, sizeof(hdr));
        WriteRaw(vals, sizeof(vals));
    }

    // ─── Mesh Resources ─────────────────────────────────────────────────

    void CreateMesh(PglMesh meshId,
                    const PglVec3* vertices, uint16_t vertexCount,
                    const PglIndex3* indices, uint16_t triangleCount,
                    bool hasUV = false,
                    const PglVec2* uvVertices = nullptr, uint16_t uvVertexCount = 0,
                    const PglIndex3* uvIndices = nullptr) {
        PglCmdCreateMeshHeader hdr{};
        hdr.meshId        = meshId;
        hdr.vertexCount   = vertexCount;
        hdr.triangleCount = triangleCount;
        hdr.flags         = hasUV ? PGL_MESH_HAS_UV : 0;

        uint32_t totalPayload = sizeof(hdr)
                              + vertexCount * sizeof(PglVec3)
                              + triangleCount * sizeof(PglIndex3);
        if (hasUV) {
            totalPayload += sizeof(uint16_t)                      // uvVertexCount
                          + uvVertexCount * sizeof(PglVec2)
                          + triangleCount * sizeof(PglIndex3);    // uvIndices
        }

        WriteCommandHeader(PGL_CMD_CREATE_MESH, static_cast<uint16_t>(totalPayload));
        WriteRaw(&hdr, sizeof(hdr));
        WriteRaw(vertices, vertexCount * sizeof(PglVec3));
        WriteRaw(indices, triangleCount * sizeof(PglIndex3));

        if (hasUV) {
            WriteRaw(&uvVertexCount, sizeof(uvVertexCount));
            WriteRaw(uvVertices, uvVertexCount * sizeof(PglVec2));
            WriteRaw(uvIndices, triangleCount * sizeof(PglIndex3));
        }
    }

    void DestroyMesh(PglMesh meshId) {
        PglCmdDestroyMesh payload{};
        payload.meshId = meshId;
        WriteCommand(PGL_CMD_DESTROY_MESH, &payload, sizeof(payload));
    }

    void UpdateVertices(PglMesh meshId, const PglVec3* vertices, uint16_t vertexCount) {
        PglCmdUpdateVerticesHeader hdr{};
        hdr.meshId      = meshId;
        hdr.vertexCount = vertexCount;

        const uint16_t totalPayload = sizeof(hdr) + vertexCount * sizeof(PglVec3);
        WriteCommandHeader(PGL_CMD_UPDATE_VERTICES, totalPayload);
        WriteRaw(&hdr, sizeof(hdr));
        WriteRaw(vertices, vertexCount * sizeof(PglVec3));
    }

    void UpdateVerticesDelta(PglMesh meshId, const PglVertexDelta* deltas, uint16_t deltaCount) {
        PglCmdUpdateVerticesDeltaHeader hdr{};
        hdr.meshId     = meshId;
        hdr.deltaCount = deltaCount;

        const uint16_t totalPayload = sizeof(hdr) + deltaCount * sizeof(PglVertexDelta);
        WriteCommandHeader(PGL_CMD_UPDATE_VERTICES_DELTA, totalPayload);
        WriteRaw(&hdr, sizeof(hdr));
        WriteRaw(deltas, deltaCount * sizeof(PglVertexDelta));
    }

    // ─── Material Resources ─────────────────────────────────────────────

    void CreateMaterial(PglMaterial materialId, PglMaterialType type,
                        PglBlendMode blendMode,
                        const void* params, uint16_t paramSize) {
        PglCmdCreateMaterialHeader hdr{};
        hdr.materialId   = materialId;
        hdr.materialType = static_cast<uint8_t>(type);
        hdr.blendMode    = static_cast<uint8_t>(blendMode);

        const uint16_t totalPayload = sizeof(hdr) + paramSize;
        WriteCommandHeader(PGL_CMD_CREATE_MATERIAL, totalPayload);
        WriteRaw(&hdr, sizeof(hdr));
        if (paramSize > 0 && params) {
            WriteRaw(params, paramSize);
        }
    }

    void UpdateMaterial(PglMaterial materialId, const void* params, uint16_t paramSize) {
        PglCmdUpdateMaterialHeader hdr{};
        hdr.materialId = materialId;

        const uint16_t totalPayload = sizeof(hdr) + paramSize;
        WriteCommandHeader(PGL_CMD_UPDATE_MATERIAL, totalPayload);
        WriteRaw(&hdr, sizeof(hdr));
        if (paramSize > 0 && params) {
            WriteRaw(params, paramSize);
        }
    }

    void DestroyMaterial(PglMaterial materialId) {
        PglCmdDestroyMaterial payload{};
        payload.materialId = materialId;
        WriteCommand(PGL_CMD_DESTROY_MATERIAL, &payload, sizeof(payload));
    }

    // ─── Texture Resources ──────────────────────────────────────────────

    void CreateTexture(PglTexture textureId,
                       uint16_t width, uint16_t height,
                       PglTextureFormat format,
                       const void* pixelData) {
        PglCmdCreateTextureHeader hdr{};
        hdr.textureId = textureId;
        hdr.width     = width;
        hdr.height    = height;
        hdr.format    = static_cast<uint8_t>(format);

        const uint16_t bpp = (format == PGL_TEX_RGB565) ? 2 : 3;
        const uint32_t pixelDataSize = static_cast<uint32_t>(width) * height * bpp;
        const uint32_t totalPayload = sizeof(hdr) + pixelDataSize;

        WriteCommandHeader(PGL_CMD_CREATE_TEXTURE, static_cast<uint16_t>(totalPayload));
        WriteRaw(&hdr, sizeof(hdr));
        WriteRaw(pixelData, pixelDataSize);
    }

    void DestroyTexture(PglTexture textureId) {
        PglCmdDestroyTexture payload{};
        payload.textureId = textureId;
        WriteCommand(PGL_CMD_DESTROY_TEXTURE, &payload, sizeof(payload));
    }

    // ─── Pixel Layout ───────────────────────────────────────────────────

    void SetPixelLayoutIrregular(PglLayout layoutId, const PglVec2* coords,
                                 uint16_t pixelCount, bool reversed = false) {
        PglCmdSetPixelLayoutHeader hdr{};
        hdr.layoutId   = layoutId;
        hdr.pixelCount = pixelCount;
        hdr.flags      = reversed ? PGL_LAYOUT_REVERSED : 0;

        const uint16_t totalPayload = sizeof(hdr) + pixelCount * sizeof(PglVec2);
        WriteCommandHeader(PGL_CMD_SET_PIXEL_LAYOUT, totalPayload);
        WriteRaw(&hdr, sizeof(hdr));
        WriteRaw(coords, pixelCount * sizeof(PglVec2));
    }

    void SetPixelLayoutRect(PglLayout layoutId, uint16_t pixelCount,
                            const PglVec2& size, const PglVec2& position,
                            uint16_t rowCount, uint16_t colCount, bool reversed = false) {
        PglCmdSetPixelLayoutHeader hdr{};
        hdr.layoutId   = layoutId;
        hdr.pixelCount = pixelCount;
        hdr.flags      = PGL_LAYOUT_RECTANGULAR | (reversed ? PGL_LAYOUT_REVERSED : 0);

        PglRectLayoutData rect{};
        rect.size     = size;
        rect.position = position;
        rect.rowCount = rowCount;
        rect.colCount = colCount;

        const uint16_t totalPayload = sizeof(hdr) + sizeof(rect);
        WriteCommandHeader(PGL_CMD_SET_PIXEL_LAYOUT, totalPayload);
        WriteRaw(&hdr, sizeof(hdr));
        WriteRaw(&rect, sizeof(rect));
    }

    // ─── GPU Memory Access ──────────────────────────────────────────────

    /**
     * @brief Write raw bytes to a specific GPU memory tier and address.
     * @param tier    Target memory tier (PglMemTier).
     * @param address Byte offset within the tier's address space.
     * @param data    Pointer to source data on the host.
     * @param size    Number of bytes to write.
     */
    void MemWrite(PglMemTier tier, uint32_t address,
                  const void* data, uint32_t size) {
        PglCmdMemWriteHeader hdr{};
        hdr.tier    = static_cast<uint8_t>(tier);
        hdr.address = address;
        hdr.size    = size;

        const uint32_t totalPayload = sizeof(hdr) + size;
        WriteCommandHeader(PGL_CMD_MEM_WRITE, static_cast<uint16_t>(totalPayload));
        WriteRaw(&hdr, sizeof(hdr));
        WriteRaw(data, size);
    }

    /**
     * @brief Request GPU to stage a block of memory for I2C readback.
     *
     * After this command is processed, the host can read the data via
     * PGL_REG_MEM_READ_DATA I2C register in 32-byte chunks.
     *
     * @param tier    Source memory tier.
     * @param address Byte offset within the tier.
     * @param size    Bytes to stage (max PGL_MEM_READ_MAX_SIZE = 4096).
     */
    void MemReadRequest(PglMemTier tier, uint32_t address, uint16_t size) {
        PglCmdMemReadRequest payload{};
        payload.tier    = static_cast<uint8_t>(tier);
        payload.address = address;
        payload.size    = (size > PGL_MEM_READ_MAX_SIZE) ? PGL_MEM_READ_MAX_SIZE : size;
        WriteCommand(PGL_CMD_MEM_READ_REQUEST, &payload, sizeof(payload));
    }

    /**
     * @brief Set the preferred memory tier for a GPU resource.
     * @param resourceClass  Resource type (mesh, material, texture, layout, generic).
     * @param resourceId     Handle of the resource.
     * @param preferredTier  Desired tier (or PGL_TIER_AUTO for GPU-managed).
     * @param pinned         If true, resource is never auto-migrated between tiers.
     */
    void SetResourceTier(PglMemResourceClass resourceClass, uint16_t resourceId,
                         PglMemTier preferredTier, bool pinned = false) {
        PglCmdSetResourceTier payload{};
        payload.resourceClass = static_cast<uint8_t>(resourceClass);
        payload.resourceId    = resourceId;
        payload.preferredTier = static_cast<uint8_t>(preferredTier);
        payload.flags         = pinned ? PGL_TIER_FLAG_PINNED : 0;
        WriteCommand(PGL_CMD_MEM_SET_RESOURCE_TIER, &payload, sizeof(payload));
    }

    /**
     * @brief Allocate a region in a specific GPU memory tier.
     *
     * The allocation result (handle + address) is available via
     * PGL_REG_MEM_ALLOC_RESULT I2C register after the command is processed.
     *
     * @param tier  Target tier (PGL_TIER_AUTO not allowed).
     * @param size  Bytes to allocate.
     * @param tag   User-defined tag for identification/debug.
     */
    void MemAlloc(PglMemTier tier, uint32_t size, uint16_t tag = 0) {
        PglCmdMemAlloc payload{};
        payload.tier = static_cast<uint8_t>(tier);
        payload.size = size;
        payload.tag  = tag;
        WriteCommand(PGL_CMD_MEM_ALLOC, &payload, sizeof(payload));
    }

    /**
     * @brief Free a previously allocated GPU memory region.
     * @param handle  Handle returned by a prior MemAlloc (read via I2C).
     */
    void MemFree(PglMemHandle handle) {
        PglCmdMemFree payload{};
        payload.handle = handle;
        WriteCommand(PGL_CMD_MEM_FREE, &payload, sizeof(payload));
    }

    /**
     * @brief Request the GPU to capture a framebuffer snapshot for readback.
     *
     * The captured data is staged for I2C readback via PGL_REG_MEM_READ_DATA.
     * At 400 kHz I2C, a 128×64 RGB565 framebuffer (16 KB) takes ~0.33 s.
     * Intended for debug/screenshots, not real-time streaming.
     *
     * @param bufferSelect  0 = front buffer (currently displayed), 1 = back buffer.
     * @param format        Output format (PGL_TEX_RGB565 or PGL_TEX_RGB888).
     */
    void FramebufferCapture(uint8_t bufferSelect = 0,
                            PglTextureFormat format = PGL_TEX_RGB565) {
        PglCmdFramebufferCapture payload{};
        payload.bufferSelect = bufferSelect;
        payload.format       = static_cast<uint8_t>(format);
        WriteCommand(PGL_CMD_FRAMEBUFFER_CAPTURE, &payload, sizeof(payload));
    }

    /**
     * @brief Copy a block of memory between GPU memory regions/tiers.
     *
     * Executes entirely on-GPU — no host data transfer needed.
     * Useful for promoting/demoting data between tiers, or duplicating
     * resources within the same tier.
     *
     * @param srcTier     Source memory tier.
     * @param srcAddress  Source byte offset.
     * @param dstTier     Destination memory tier.
     * @param dstAddress  Destination byte offset.
     * @param size        Bytes to copy.
     */
    void MemCopy(PglMemTier srcTier, uint32_t srcAddress,
                 PglMemTier dstTier, uint32_t dstAddress, uint32_t size) {
        PglCmdMemCopy payload{};
        payload.srcTier    = static_cast<uint8_t>(srcTier);
        payload.srcAddress = srcAddress;
        payload.dstTier    = static_cast<uint8_t>(dstTier);
        payload.dstAddress = dstAddress;
        payload.size       = size;
        WriteCommand(PGL_CMD_MEM_COPY, &payload, sizeof(payload));
    }

    // ─── Memory Pool Commands (M11) ─────────────────────────────────────

    /**
     * @brief Create a fixed-size block pool in GPU memory.
     *
     * Allocates a contiguous region from the specified tier's free-list and
     * subdivides it into `blockCount` blocks of `blockSize` bytes each.
     * The pool handle is available via PGL_REG_MEM_ALLOC_RESULT I2C.
     *
     * @param tier        Target tier (PGL_TIER_AUTO not allowed).
     * @param blockSize   Size of each block (4–4096, should be power-of-2).
     * @param blockCount  Number of blocks to pre-allocate.
     * @param tag         User-defined tag for debug identification.
     */
    void MemPoolCreate(PglMemTier tier, uint16_t blockSize,
                       uint16_t blockCount, uint16_t tag = 0) {
        PglCmdMemPoolCreate payload{};
        payload.tier       = static_cast<uint8_t>(tier);
        payload.blockSize  = blockSize;
        payload.blockCount = blockCount;
        payload.tag        = tag;
        WriteCommand(PGL_CMD_MEM_POOL_CREATE, &payload, sizeof(payload));
    }

    /**
     * @brief Allocate one block from a memory pool.
     *
     * The block index is returned via PGL_REG_MEM_ALLOC_RESULT I2C register.
     * O(1) — pops from the pool's singly-linked free list.
     *
     * @param poolHandle  Pool handle (from prior create).
     */
    void MemPoolAlloc(PglPool poolHandle) {
        PglCmdMemPoolAlloc payload{};
        payload.poolHandle = poolHandle;
        WriteCommand(PGL_CMD_MEM_POOL_ALLOC, &payload, sizeof(payload));
    }

    /**
     * @brief Free one block back to a memory pool.
     *
     * O(1) — pushes the block index onto the pool's free list.
     *
     * @param poolHandle  Pool handle.
     * @param blockIndex  Block to free (0-based index).
     */
    void MemPoolFree(PglPool poolHandle, uint16_t blockIndex) {
        PglCmdMemPoolFree payload{};
        payload.poolHandle = poolHandle;
        payload.blockIndex = blockIndex;
        WriteCommand(PGL_CMD_MEM_POOL_FREE, &payload, sizeof(payload));
    }

    /**
     * @brief Destroy a memory pool and return its backing memory to the tier.
     * @param poolHandle  Pool to destroy.
     */
    void MemPoolDestroy(PglPool poolHandle) {
        PglCmdMemPoolDestroy payload{};
        payload.poolHandle = poolHandle;
        WriteCommand(PGL_CMD_MEM_POOL_DESTROY, &payload, sizeof(payload));
    }

    // ─── Display Configuration Commands (M11) ───────────────────────────

    /**
     * @brief Configure a display driver slot on the GPU.
     *
     * Sends CMD_DISPLAY_CONFIGURE to activate, deactivate, or reconfigure
     * a display output. The GPU's DisplayManager validates PIO allocation
     * and rejects conflicting driver combinations.
     *
     * @param displayId    Display slot (0–PGL_MAX_DISPLAYS-1).
     * @param displayType  PglDisplayType to activate.
     * @param width        Display width (0 = driver default).
     * @param height       Display height (0 = driver default).
     * @param pixelFormat  PglDisplayPixelFormat.
     * @param flags        PglDisplayConfigFlags bitmask.
     * @param brightness   Initial brightness 0–255.
     */
    void DisplayConfigure(PglDisplay displayId, PglDisplayType displayType,
                          uint16_t width = 0, uint16_t height = 0,
                          PglDisplayPixelFormat pixelFormat = PGL_PIXFMT_RGB565,
                          uint8_t flags = PGL_DISP_FLAG_ENABLED,
                          uint8_t brightness = 128) {
        PglCmdDisplayConfigure payload{};
        payload.displayId   = displayId;
        payload.displayType = static_cast<uint8_t>(displayType);
        payload.width       = width;
        payload.height      = height;
        payload.pixelFormat = static_cast<uint8_t>(pixelFormat);
        payload.flags       = flags;
        payload.brightness  = brightness;
        payload.reserved    = 0;
        WriteCommand(PGL_CMD_DISPLAY_CONFIGURE, &payload, sizeof(payload));
    }

    /**
     * @brief Set a partial-update region for a display.
     *
     * Useful for OLED/LCD displays that support windowed updates.
     * The region is used for the next SwapBuffers() call on that display.
     *
     * @param displayId  Display slot.
     * @param x          Left edge of region.
     * @param y          Top edge of region.
     * @param w          Width of region.
     * @param h          Height of region.
     */
    void DisplaySetRegion(PglDisplay displayId,
                          uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h) {
        PglCmdDisplaySetRegion payload{};
        payload.displayId = displayId;
        payload.x = x;
        payload.y = y;
        payload.w = w;
        payload.h = h;
        WriteCommand(PGL_CMD_DISPLAY_SET_REGION, &payload, sizeof(payload));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // M12: 2D Layer & Drawing Commands (0xA0 – 0xAC)
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Create a 2D compositing layer with its own framebuffer.
     *
     * Layer 0 is reserved for the 3D pipeline. Layers 1–7 are user-created.
     * The GPU-side LayerSlot allocates a framebuffer of width×height×2 bytes.
     *
     * @param layerId     Layer slot (1–7).
     * @param width       Layer framebuffer width.
     * @param height      Layer framebuffer height.
     * @param pixelFormat Pixel format (usually PGL_PIXFMT_RGB565).
     * @param blendMode   PglLayerBlendMode.
     * @param opacity     Initial opacity (0–255).
     */
    void LayerCreate(PglLayer layerId, uint16_t width, uint16_t height,
                     uint8_t pixelFormat = 0 /*RGB565*/,
                     PglLayerBlendMode blendMode = PGL_LAYER_BLEND_ALPHA,
                     uint8_t opacity = 255) {
        PglCmdLayerCreate payload{};
        payload.layerId     = layerId;
        payload.width       = width;
        payload.height      = height;
        payload.pixelFormat = pixelFormat;
        payload.blendMode   = static_cast<uint8_t>(blendMode);
        payload.opacity     = opacity;
        WriteCommand(PGL_CMD_LAYER_CREATE, &payload, sizeof(payload));
    }

    /**
     * @brief Destroy a 2D layer and free its framebuffer.
     * @param layerId  Layer slot to destroy (1–7).
     */
    void LayerDestroy(PglLayer layerId) {
        PglCmdLayerDestroy payload{};
        payload.layerId = layerId;
        WriteCommand(PGL_CMD_LAYER_DESTROY, &payload, sizeof(payload));
    }

    /**
     * @brief Update layer compositing properties (opacity, blend mode, offset).
     */
    void LayerSetProps(PglLayer layerId, uint8_t opacity,
                       PglLayerBlendMode blendMode = PGL_LAYER_BLEND_ALPHA,
                       int16_t offsetX = 0, int16_t offsetY = 0) {
        PglCmdLayerSetProps payload{};
        payload.layerId   = layerId;
        payload.opacity   = opacity;
        payload.blendMode = static_cast<uint8_t>(blendMode);
        payload.offsetX   = offsetX;
        payload.offsetY   = offsetY;
        WriteCommand(PGL_CMD_LAYER_SET_PROPS, &payload, sizeof(payload));
    }

    /**
     * @brief Draw a filled or outlined rectangle on a 2D layer.
     */
    void DrawRect2D(PglLayer layerId, int16_t x, int16_t y,
                    uint16_t w, uint16_t h,
                    uint16_t color, bool filled = true) {
        PglCmdDrawRect2D payload{};
        payload.layerId = layerId;
        payload.x       = x;
        payload.y       = y;
        payload.w       = w;
        payload.h       = h;
        payload.color   = color;
        payload.filled  = filled ? 1 : 0;
        WriteCommand(PGL_CMD_DRAW_RECT_2D, &payload, sizeof(payload));
    }

    /**
     * @brief Draw a line on a 2D layer.
     */
    void DrawLine2D(PglLayer layerId, int16_t x0, int16_t y0,
                    int16_t x1, int16_t y1, uint16_t color) {
        PglCmdDrawLine2D payload{};
        payload.layerId = layerId;
        payload.x0      = x0;
        payload.y0      = y0;
        payload.x1      = x1;
        payload.y1      = y1;
        payload.color   = color;
        WriteCommand(PGL_CMD_DRAW_LINE_2D, &payload, sizeof(payload));
    }

    /**
     * @brief Draw a filled or outlined circle on a 2D layer.
     */
    void DrawCircle2D(PglLayer layerId, int16_t cx, int16_t cy,
                      uint16_t radius, uint16_t color, bool filled = true) {
        PglCmdDrawCircle2D payload{};
        payload.layerId = layerId;
        payload.cx      = cx;
        payload.cy      = cy;
        payload.radius  = radius;
        payload.color   = color;
        payload.filled  = filled ? 1 : 0;
        WriteCommand(PGL_CMD_DRAW_CIRCLE_2D, &payload, sizeof(payload));
    }

    /**
     * @brief Blit a texture to a 2D layer position.
     *
     * @param textureId  Source texture handle (must be pre-loaded via CreateTexture).
     * @param flags      PglSpriteFlags bitmask (flip H/V, source is sequence).
     */
    void DrawSprite(PglLayer layerId, int16_t x, int16_t y,
                    PglTexture textureId, uint8_t flags = 0) {
        PglCmdDrawSprite payload{};
        payload.layerId   = layerId;
        payload.x         = x;
        payload.y         = y;
        payload.textureId = textureId;
        payload.flags     = flags;
        WriteCommand(PGL_CMD_DRAW_SPRITE, &payload, sizeof(payload));
    }

    /**
     * @brief Clear a 2D layer to a solid color.
     */
    void LayerClear(PglLayer layerId, uint16_t color = 0x0000) {
        PglCmdLayerClear payload{};
        payload.layerId = layerId;
        payload.color   = color;
        WriteCommand(PGL_CMD_LAYER_CLEAR, &payload, sizeof(payload));
    }

    /**
     * @brief Draw a rounded rectangle on a 2D layer.
     */
    void DrawRoundedRect(PglLayer layerId, int16_t x, int16_t y,
                         uint16_t w, uint16_t h, uint16_t radius,
                         uint16_t color, bool filled = true) {
        PglCmdDrawRoundedRect payload{};
        payload.layerId = layerId;
        payload.x       = x;
        payload.y       = y;
        payload.w       = w;
        payload.h       = h;
        payload.radius  = radius;
        payload.color   = color;
        payload.filled  = filled ? 1 : 0;
        WriteCommand(PGL_CMD_DRAW_ROUNDED_RECT, &payload, sizeof(payload));
    }

    /**
     * @brief Draw an arc on a 2D layer.
     */
    void DrawArc(PglLayer layerId, int16_t cx, int16_t cy,
                 uint16_t radius, int16_t startAngleDeg, int16_t endAngleDeg,
                 uint16_t color) {
        PglCmdDrawArc payload{};
        payload.layerId       = layerId;
        payload.cx            = cx;
        payload.cy            = cy;
        payload.radius        = radius;
        payload.startAngleDeg = startAngleDeg;
        payload.endAngleDeg   = endAngleDeg;
        payload.color         = color;
        WriteCommand(PGL_CMD_DRAW_ARC, &payload, sizeof(payload));
    }

    /**
     * @brief Draw a filled 2D triangle on a layer.
     */
    void DrawTriangle2D(PglLayer layerId,
                        int16_t x0, int16_t y0,
                        int16_t x1, int16_t y1,
                        int16_t x2, int16_t y2,
                        uint16_t color) {
        PglCmdDrawTriangle2D payload{};
        payload.layerId = layerId;
        payload.x0 = x0; payload.y0 = y0;
        payload.x1 = x1; payload.y1 = y1;
        payload.x2 = x2; payload.y2 = y2;
        payload.color = color;
        WriteCommand(PGL_CMD_DRAW_TRIANGLE_2D, &payload, sizeof(payload));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // M12: Memory Defrag (0x3C)
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Trigger memory defragmentation on a tier.
     *
     * @param tier       PglMemTier to defragment (PGL_TIER_AUTO = all tiers).
     * @param mode       PGL_DEFRAG_INCREMENTAL or PGL_DEFRAG_URGENT.
     * @param maxMoveKB  Max kilobytes to relocate (incremental mode).
     */
    void MemDefrag(uint8_t tier = 0xFF /*AUTO*/, PglDefragMode mode = PGL_DEFRAG_INCREMENTAL,
                   uint16_t maxMoveKB = 32) {
        PglCmdMemDefrag payload{};
        payload.tier      = tier;
        payload.mode      = static_cast<uint8_t>(mode);
        payload.maxMoveKB = maxMoveKB;
        WriteCommand(PGL_CMD_MEM_DEFRAG, &payload, sizeof(payload));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // M12: Direct Framebuffer Write (0x45)
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Write a rectangle of RGB565 pixels directly to a layer or back buffer.
     *
     * @param layerId  Target layer (0xFF = primary 3D back buffer).
     * @param x        Destination X.
     * @param y        Destination Y.
     * @param w        Width in pixels.
     * @param h        Height in pixels.
     * @param pixels   Pointer to w×h RGB565 pixel data.
     */
    void WriteFramebuffer(PglLayer layerId, int16_t x, int16_t y,
                          uint16_t w, uint16_t h,
                          const uint16_t* pixels) {
        PglCmdWriteFramebufferHeader hdr{};
        hdr.layerId = layerId;
        hdr.x       = x;
        hdr.y       = y;
        hdr.w       = w;
        hdr.h       = h;
        uint32_t pixelBytes = uint32_t(w) * h * sizeof(uint16_t);
        uint16_t totalPayload = sizeof(hdr) + pixelBytes;
        WriteCommandHeader(PGL_CMD_WRITE_FRAMEBUFFER, totalPayload);
        WriteRaw(&hdr, sizeof(hdr));
        WriteRaw(pixels, pixelBytes);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // M12: Resource Persistence (0x46 – 0x48)
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Queue a resource for flash persistence (writeback).
     *
     * @param resourceClass  PglMemResourceClass.
     * @param resourceId     Resource handle.
     * @param overwrite      Overwrite existing flash entry if present.
     */
    void PersistResource(uint8_t resourceClass, uint16_t resourceId,
                         bool overwrite = true) {
        PglCmdPersistResource payload{};
        payload.resourceClass = resourceClass;
        payload.resourceId    = resourceId;
        payload.flags         = overwrite ? PGL_PERSIST_OVERWRITE : 0;
        WriteCommand(PGL_CMD_PERSIST_RESOURCE, &payload, sizeof(payload));
    }

    /**
     * @brief Restore a resource from flash manifest to VRAM.
     */
    void RestoreResource(uint8_t resourceClass, uint16_t resourceId) {
        PglCmdRestoreResource payload{};
        payload.resourceClass = resourceClass;
        payload.resourceId    = resourceId;
        WriteCommand(PGL_CMD_RESTORE_RESOURCE, &payload, sizeof(payload));
    }

    /**
     * @brief Query persistence status for a resource (or entire manifest).
     *
     * Result available via PGL_REG_MEM_PERSIST_STATUS I2C register.
     *
     * @param resourceClass  0xFF to query manifest-level status.
     * @param resourceId     Ignored if class = 0xFF.
     */
    void QueryPersistence(uint8_t resourceClass = 0xFF, uint16_t resourceId = 0) {
        PglCmdQueryPersistence payload{};
        payload.resourceClass = resourceClass;
        payload.resourceId    = resourceId;
        WriteCommand(PGL_CMD_QUERY_PERSISTENCE, &payload, sizeof(payload));
    }

private:
    // ─── Internal Helpers ───────────────────────────────────────────────

    void WriteRaw(const void* data, size_t size) {
        if (overflow_) return;
        if (writePos_ + size > capacity_) {
            overflow_ = true;
            return;
        }
        std::memcpy(buffer_ + writePos_, data, size);
        writePos_ += size;
    }

    void WriteCommandHeader(uint8_t opcode, uint16_t payloadLength) {
        PglCommandHeader hdr{};
        hdr.opcode        = opcode;
        hdr.payloadLength = payloadLength;
        WriteRaw(&hdr, sizeof(hdr));
        commandCount_++;
    }

    void WriteCommand(uint8_t opcode, const void* payload, uint16_t payloadLength) {
        WriteCommandHeader(opcode, payloadLength);
        WriteRaw(payload, payloadLength);
    }

    uint8_t* buffer_;
    size_t   capacity_;
    size_t   writePos_       = 0;
    size_t   frameHeaderPos_ = 0;
    uint16_t commandCount_   = 0;
    uint32_t currentFrameNumber_ = 0;
    bool     overflow_       = false;
};
