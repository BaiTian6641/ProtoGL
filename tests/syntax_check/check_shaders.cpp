// ProtoGL syntax/AST compile check — à-la-carte headers smoke TU.
//
// This file is never linked or executed. It covers the ProtoGL headers that
// are NOT part of the ProtoGL.h umbrella and are included separately by
// consumers (shader compiler, shader math backend, job scheduler), so that
//   g++ -std=gnu++17 -fsyntax-only -Wall -Wextra
// semantically checks them as portable C++17 on a desktop toolchain.

#include "../../src/PglShaderCompiler.h"        // pulls in PglShaderBytecode.h
#include "../../src/PglShaderBackend.h"
#include "../../src/PglJobScheduler_SingleCore.h" // pulls in PglJobScheduler.h

#include <cstddef>
#include <cstdint>

namespace {

void jobFunc(void* ctx) {
    (void)ctx;
}

void idleFunc() {
}

} // namespace

// Never called — compiled only.
void protogl_shader_api_smoke() {
    // ─── PSB bytecode format structs + constants ────────────────────────
    static_assert(sizeof(PglShaderProgramHeader) == 16, "PSB header changed");
    static_assert(sizeof(PglUniformDescriptor) == 8, "PSB uniform desc changed");
    static_assert(sizeof(PglShaderInstruction) == 4, "PSB instruction changed");
    static_assert(PSB_MAGIC == 0x50534231u, "PSB magic changed");

    PglShaderProgramHeader psbHdr{};
    psbHdr.magic        = PSB_MAGIC;
    psbHdr.version      = PSB_VERSION;
    psbHdr.flags        = PSB_FLAG_NEEDS_SCRATCH_COPY;
    psbHdr.constCount   = 1;
    psbHdr.uniformCount = PSB_MAX_UNIFORMS;
    psbHdr.instrCount   = PSB_MAX_INSTRUCTIONS;
    psbHdr.nameHash     = PsbFnv1a("smoke");
    (void)psbHdr;

    PglUniformDescriptor udesc{};
    udesc.type = PSB_UNIFORM_VEC4;
    udesc.slot = PSB_USER_UNIFORM_START;
    (void)udesc;

    PglShaderInstruction instr{};
    instr.opcode = PSB_OP_ADD;
    instr.dst    = PSB_REG_OUT_R;
    instr.srcA   = PSB_REG_IN_R;
    instr.srcB   = static_cast<uint8_t>(PSB_OP_LITERAL_BASE + 2); // literal 1.0

    float regs[PSB_NUM_REGISTERS]   = {};
    float uniforms[PSB_MAX_UNIFORMS] = {};
    float consts[PSB_MAX_CONSTANTS] = {};
    regs[PSB_REG_IN_R] = 0.25f;
    const float resolved = PsbResolveOperand(instr.srcA, regs, uniforms, consts);
    const float lit      = PsbResolveOperand(instr.srcB, regs, uniforms, consts);
    (void)resolved; (void)lit;
    (void)PSB_LITERALS[PSB_LITERAL_COUNT - 1];

    // ─── PGLSL → PSB compiler entry point ───────────────────────────────
    static const char kShaderSrc[] =
        "uniform float u_gain;\n"
        "void main() {\n"
        "    float r = gl_FragCoord.x * 0.01;\n"
        "    gl_FragColor = vec4(r, r, r, 1.0);\n"
        "}\n";
    PglShaderCompiler::CompileResult result =
        PglShaderCompiler::Compile(kShaderSrc, sizeof(kShaderSrc) - 1);
    (void)result.success;
    (void)result.bytecodeSize;
    (void)result.bytecode[0];
    (void)result.errorMsg[0];
    (void)result.errorLine;

    // ─── Shader math backend (scalar-float reference path) ──────────────
    namespace sb = PglShaderBackend;
    float ox = 0.0f, oy = 0.0f, oz = 0.0f;
    const float m = sb::Fma(2.0f, 3.0f, 1.0f);
    const float s = sb::Sin(0.5f) + sb::Cos(0.5f) + sb::Sqrt(4.0f)
                  + sb::Atan2(1.0f, 2.0f) + sb::Pow(2.0f, 3.0f)
                  + sb::Exp(0.0f) + sb::Log(1.0f) + sb::Rsqrt(4.0f)
                  + sb::Tan(0.25f) + sb::Asin(0.5f) + sb::Acos(0.5f)
                  + sb::Atan(1.0f);
    const float t = sb::Clamp(sb::Mix(0.0f, 10.0f, 0.5f), 0.0f, 10.0f)
                  + sb::Min(1.0f, 2.0f) + sb::Max(1.0f, 2.0f)
                  + sb::Step(0.5f, 0.75f) + sb::Smoothstep(0.0f, 1.0f, 0.5f)
                  + sb::Abs(-1.0f) + sb::Floor(1.5f) + sb::Ceil(1.5f)
                  + sb::Sign(-2.0f) + sb::Fract(1.25f) + sb::Mod(5.0f, 3.0f);
    const float d = sb::Dot3(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f)
                  + sb::Dot2(1.0f, 0.0f, 0.0f, 1.0f)
                  + sb::Len3(1.0f, 2.0f, 2.0f) + sb::Len2(3.0f, 4.0f)
                  + sb::Dist2(0.0f, 0.0f, 1.0f, 1.0f)
                  + sb::Div(1.0f, 2.0f) + sb::Add(1.0f, 2.0f)
                  + sb::Sub(2.0f, 1.0f) + sb::Mul(2.0f, 3.0f) + sb::Neg(1.0f);
    sb::Cross(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, ox, oy, oz);
    sb::Norm3(1.0f, 2.0f, 2.0f, ox, oy, oz);
    sb::Norm2(3.0f, 4.0f, ox, oy);
    float fr = 0.0f, fg = 0.0f, fb = 0.0f;
    sb::UnpackRGB565(0xF800, fr, fg, fb);
    const uint16_t packed  = sb::PackRGB565(fr, fg, fb);
    const uint16_t packedI = sb::PackRGB565i(31, 63, 31);
    const uint16_t fb2x2[4] = { 0xF800, 0x07E0, 0x001F, 0xFFFF };
    sb::TexSample(fb2x2, 2, 2, 0.25f, 0.75f, fr, fg, fb);
    (void)m; (void)s; (void)t; (void)d;
    (void)packed; (void)packedI;
    (void)sb::R5(packed); (void)sb::G6(packed); (void)sb::B5(packed);
    (void)sb::Clamp5(42); (void)sb::Clamp6(42);
    (void)ox; (void)oy; (void)oz; (void)fr; (void)fg; (void)fb;

    // ─── Single-core job scheduler (via the abstract interface) ─────────
    PglJobScheduler_SingleCore singleCore;
    PglJobScheduler* scheduler = &singleCore;
    int ctx = 0;
    const PglJob jobs[2] = {
        { jobFunc, &ctx },
        { jobFunc, &ctx },
    };
    scheduler->Submit(jobs, 2);
    scheduler->WaitAll(idleFunc);
    scheduler->WaitAll();
    (void)scheduler->WorkerCount();
}
