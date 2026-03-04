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
 * ProtoGL API Specification v0.3 — FROZEN
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

    // ─── Effect ─────────────────────────────────────────────────────────

    void SetEffect(PglCamera cameraId, uint8_t effectType, float ratio) {
        PglCmdSetEffect payload{};
        payload.cameraId  = cameraId;
        payload.effectType = effectType;
        payload.ratio      = ratio;
        WriteCommand(PGL_CMD_SET_EFFECT, &payload, sizeof(payload));
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
