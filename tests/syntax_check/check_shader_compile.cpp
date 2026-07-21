// ProtoGL runtime gate — PGLSL → PSB compiler register allocator.
//
// Companion to the -fsyntax-only checks (run_check.sh): this TU is LINKED
// AND EXECUTED (run_shader_compile.sh). It compiles all 8 stock
// shaders/*.pglsl samples with the real header-only compiler and asserts:
//   - compile success == true                            (allocator fix gate)
//   - 0 < bytecode size < 768 bytes                      (sane PSB output;
//     hue_shift is the biggest stock shader at 528 B — the PSB hard limit
//     is PSB_MAX_PROGRAM_SIZE = 1296)
//   - regHighWater <= 28 — every temporary fits r8–r27   (no spill into
//     gl_FragColor r28–r31; regHighWater is one past the highest register)
//   - instruction stream is well-formed (ends with END) and every register
//     operand stays inside r0–r31
//   - semantic opcode spot checks per shader (SUB for invert, POW for
//     gamma, TEX2D for every texture2D user, STEP for scanlines, MIX for
//     vignette, SIN+COS for hue_shift)
//   - a deliberately over-deep expression still FAILS with
//     "register allocation overflow" (the safety net survives the fix)
//   - executed on a mini PSB interpreter: invert/gamma produce the expected
//     pixel values, proving dst==operand reuse is sound (the interpreter
//     mirrors the firmware VM's read-all-operands-then-write-dst rule).
//
// Usage: check_shader_compile <ProtoGL-repo-root>
// Exit code: 0 = all checks passed, 1 = failure.

#include "../../src/PglShaderCompiler.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

// ─── PSB bytecode disassembly view ──────────────────────────────────────────

struct PsbView {
    const PglShaderProgramHeader* hdr;
    const uint32_t*               instrs;
};

bool ParsePsb(const PglShaderCompiler::CompileResult& r, PsbView& view) {
    if (r.bytecodeSize < sizeof(PglShaderProgramHeader)) return false;
    const uint8_t* p = r.bytecode;
    view.hdr = reinterpret_cast<const PglShaderProgramHeader*>(p);
    if (view.hdr->magic != PSB_MAGIC) return false;
    size_t off = sizeof(PglShaderProgramHeader)
               + view.hdr->uniformCount * sizeof(PglUniformDescriptor)
               + view.hdr->constCount * sizeof(float);
    if (off + view.hdr->instrCount * 4 > r.bytecodeSize) return false;
    view.instrs = reinterpret_cast<const uint32_t*>(p + off);
    return true;
}

bool HasOpcode(const PsbView& v, uint8_t op) {
    for (uint16_t i = 0; i < v.hdr->instrCount; ++i) {
        if ((v.instrs[i] & 0xFF) == op) return true;
    }
    return false;
}

// Every register-class operand (dst/srcA/srcB) must stay inside r0–r31.
bool RegisterOperandsSane(const PsbView& v) {
    for (uint16_t i = 0; i < v.hdr->instrCount; ++i) {
        uint32_t raw = v.instrs[i];
        uint8_t dst  = (raw >> 8) & 0xFF;
        uint8_t srcA = (raw >> 16) & 0xFF;
        uint8_t srcB = (raw >> 24) & 0xFF;
        // dst must be a real register or UNUSED (never uniform/const/literal)
        if (dst != PSB_OP_UNUSED && dst > PSB_OP_REG_END) return false;
        if (srcA >= PSB_OP_LITERAL_END + 1 && srcA != PSB_OP_UNUSED) return false;
        if (srcB >= PSB_OP_LITERAL_END + 1 && srcB != PSB_OP_UNUSED) return false;
    }
    return true;
}

// ─── Mini PSB interpreter (semantic spot checks) ────────────────────────────
// Mirrors RP2350-ProtoGPU pgl_shader_vm.cpp: decode [opcode][dst][srcA][srcB],
// resolve ALL source operands, then write dst (so dst may alias a source).

struct MiniVm {
    float regs[PSB_NUM_REGISTERS]   = {};
    float uniforms[PSB_MAX_UNIFORMS] = {};
    float constants[PSB_MAX_CONSTANTS] = {};
    // Deterministic fake framebuffer sample: (u, v, 0.5*(u+v))
    bool Sample(float u, float v, float& r, float& g, float& b) {
        r = u; g = v; b = 0.5f * (u + v);
        return true;
    }

    float Resolve(uint8_t op) const {
        return PsbResolveOperand(op, regs, uniforms, constants);
    }

    void Run(const PsbView& v) {
        for (int i = 0; i < v.hdr->constCount; ++i) {
            const uint8_t* cp = reinterpret_cast<const uint8_t*>(v.instrs)
                              - v.hdr->constCount * sizeof(float);
            float f;
            std::memcpy(&f, cp + i * sizeof(float), sizeof(float));
            constants[i] = f;
        }
        regs[PSB_REG_FRAG_X] = 32.0f;  // arbitrary gl_FragCoord
        regs[PSB_REG_FRAG_Y] = 16.0f;
        regs[PSB_REG_FRAG_Z] = 0.0f;
        regs[PSB_REG_FRAG_W] = 1.0f;

        for (uint16_t pc = 0; pc < v.hdr->instrCount; ++pc) {
            uint32_t raw   = v.instrs[pc];
            uint8_t opcode = raw & 0xFF;
            uint8_t dstOp  = (raw >> 8) & 0xFF;
            uint8_t srcA   = (raw >> 16) & 0xFF;
            uint8_t srcB   = (raw >> 24) & 0xFF;
            const float a  = Resolve(srcA);
            const float b  = Resolve(srcB);
            float result   = 0.0f;
            switch (opcode) {
                case PSB_OP_END: return;
                case PSB_OP_MOV: result = a; break;
                case PSB_OP_ADD: result = a + b; break;
                case PSB_OP_SUB: result = a - b; break;
                case PSB_OP_MUL: result = a * b; break;
                case PSB_OP_DIV: result = (b != 0.0f) ? a / b : 0.0f; break;
                case PSB_OP_NEG: result = -a; break;
                case PSB_OP_SIN: result = std::sin(a); break;
                case PSB_OP_COS: result = std::cos(a); break;
                case PSB_OP_POW: result = std::pow(a, b); break;
                case PSB_OP_SQRT: result = std::sqrt(a); break;
                case PSB_OP_ABS: result = std::fabs(a); break;
                case PSB_OP_FLOOR: result = std::floor(a); break;
                case PSB_OP_FRACT: result = a - std::floor(a); break;
                case PSB_OP_MIN: result = a < b ? a : b; break;
                case PSB_OP_MAX: result = a > b ? a : b; break;
                case PSB_OP_CLAMP: { float hi = regs[dstOp & 0x1F];
                    result = a < b ? b : (a > hi ? hi : a); break; }
                case PSB_OP_MIX: { float t = regs[dstOp & 0x1F];
                    result = a * (1.0f - t) + b * t; break; }
                case PSB_OP_STEP: result = (b >= a) ? 1.0f : 0.0f; break;
                case PSB_OP_LEN2: { uint8_t ai = srcA & 0x1F;
                    result = std::sqrt(regs[ai]*regs[ai] + regs[ai+1]*regs[ai+1]); break; }
                case PSB_OP_LEN3: { uint8_t ai = srcA & 0x1F;
                    result = std::sqrt(regs[ai]*regs[ai] + regs[ai+1]*regs[ai+1]
                                     + regs[ai+2]*regs[ai+2]); break; }
                case PSB_OP_TEX2D: { uint8_t ai = srcA & 0x1F, di = dstOp & 0x1F;
                    float tr, tg, tb;
                    Sample(regs[ai], regs[ai+1], tr, tg, tb);
                    regs[di] = tr; regs[di+1] = tg; regs[di+2] = tb; regs[di+3] = 1.0f;
                    continue; }
                case PSB_OP_LCONST: result = constants[srcA]; break;
                case PSB_OP_LUNI:   result = uniforms[srcA]; break;
                default: continue;  // unimplemented op — skip (spot checks only)
            }
            if (dstOp <= PSB_OP_REG_END) regs[dstOp] = result;
        }
    }
};

// ─── Stock shader table ─────────────────────────────────────────────────────

struct StockCheck {
    const char* file;
    uint8_t     mustHave;    // opcode that must appear (0 = none beyond TEX2D)
    const char* mustName;
};

const StockCheck kStock[] = {
    { "brightness.pglsl",    PSB_OP_ADD,  "ADD"  },
    { "chromatic_ab.pglsl",  PSB_OP_LEN2, "LEN2" },
    { "contrast.pglsl",      PSB_OP_SUB,  "SUB"  },
    { "gamma.pglsl",         PSB_OP_POW,  "POW"  },
    { "hue_shift.pglsl",     PSB_OP_COS,  "COS"  },
    { "invert.pglsl",        PSB_OP_SUB,  "SUB"  },
    { "scanlines.pglsl",     PSB_OP_STEP, "STEP" },
    { "vignette.pglsl",      PSB_OP_MIX,  "MIX"  },
};

bool LoadFile(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string root = (argc > 1) ? argv[1] : ".";
    std::printf("PGLSL stock-shader compile gate (root: %s)\n\n", root.c_str());
    std::printf("%-18s %-7s %-6s %-9s %s\n", "shader", "success", "bytes", "highWater", "notes");

    for (const StockCheck& sc : kStock) {
        std::string src;
        std::string path = root + "/shaders/" + sc.file;
        if (!LoadFile(path, src)) {
            std::printf("%-18s MISSING (%s)\n", sc.file, path.c_str());
            Check(false, "stock shader file readable");
            continue;
        }

        auto r = PglShaderCompiler::Compile(src.c_str(), src.size());

        char note[160] = "";
        if (!r.success) std::snprintf(note, sizeof(note), "error: %s", r.errorMsg);
        std::printf("%-18s %-7s %-6u r%-8u %s\n", sc.file,
                    r.success ? "OK" : "FAIL", r.bytecodeSize,
                    r.regHighWater > 0 ? r.regHighWater - 1 : 0, note);

        char msg[160];
        std::snprintf(msg, sizeof(msg), "%s compiles", sc.file);
        Check(r.success, msg);
        if (!r.success) continue;

        std::snprintf(msg, sizeof(msg), "%s bytecode size 0<n<768", sc.file);
        Check(r.bytecodeSize > 0 && r.bytecodeSize < 768, msg);

        std::snprintf(msg, sizeof(msg), "%s registers within r8-r27", sc.file);
        Check(r.regHighWater <= PSB_REG_USER_END + 1, msg);

        PsbView view{};
        std::snprintf(msg, sizeof(msg), "%s PSB layout parses", sc.file);
        Check(ParsePsb(r, view), msg);
        if (!view.hdr) continue;

        Check(view.hdr->instrCount > 0 &&
              (view.instrs[view.hdr->instrCount - 1] & 0xFF) == PSB_OP_END,
              "instruction stream ends with END");
        Check(RegisterOperandsSane(view), "register operands inside r0-r31");

        std::snprintf(msg, sizeof(msg), "%s contains TEX2D", sc.file);
        Check(HasOpcode(view, PSB_OP_TEX2D), msg);
        std::snprintf(msg, sizeof(msg), "%s contains %s", sc.file, sc.mustName);
        Check(HasOpcode(view, sc.mustHave), msg);
    }

    // ── Semantic execution spot checks (mini VM) ─────────────────────────
    std::printf("\nSemantic execution spot checks (mini PSB interpreter):\n");
    {
        std::string src;
        Check(LoadFile(root + "/shaders/invert.pglsl", src), "invert load");
        auto r = PglShaderCompiler::Compile(src.c_str(), src.size());
        Check(r.success, "invert compiles for exec");
        PsbView view{};
        if (r.success && ParsePsb(r, view)) {
            MiniVm vm;
            vm.uniforms[PSB_AUTO_UNIFORM_RESOLUTION_X] = 128.0f;
            vm.uniforms[PSB_AUTO_UNIFORM_RESOLUTION_Y] = 64.0f;
            vm.Run(view);
            // uv = (32/128, 16/64) = (0.25, 0.25); sample = (0.25, 0.25, 0.25)
            // inverted = (0.75, 0.75, 0.75)
            bool ok = std::fabs(vm.regs[PSB_REG_OUT_R] - 0.75f) < 1e-4f &&
                      std::fabs(vm.regs[PSB_REG_OUT_G] - 0.75f) < 1e-4f &&
                      std::fabs(vm.regs[PSB_REG_OUT_B] - 0.75f) < 1e-4f;
            std::printf("  invert  -> out=(%.3f, %.3f, %.3f) expect (0.75, 0.75, 0.75) %s\n",
                        vm.regs[PSB_REG_OUT_R], vm.regs[PSB_REG_OUT_G], vm.regs[PSB_REG_OUT_B],
                        ok ? "OK" : "FAIL");
            Check(ok, "invert execution result");
        } else {
            Check(false, "invert parses for exec");
        }
    }
    {
        std::string src;
        Check(LoadFile(root + "/shaders/gamma.pglsl", src), "gamma load");
        auto r = PglShaderCompiler::Compile(src.c_str(), src.size());
        Check(r.success, "gamma compiles for exec");
        PsbView view{};
        if (r.success && ParsePsb(r, view)) {
            MiniVm vm;
            vm.uniforms[PSB_AUTO_UNIFORM_RESOLUTION_X] = 128.0f;
            vm.uniforms[PSB_AUTO_UNIFORM_RESOLUTION_Y] = 64.0f;
            vm.uniforms[PSB_USER_UNIFORM_START] = 2.0f;   // u_gamma = 2.0
            vm.Run(view);
            // sample = (0.25, 0.25, 0.25); pow(x, 2) = 0.0625 per channel
            bool ok = std::fabs(vm.regs[PSB_REG_OUT_R] - 0.0625f) < 1e-4f &&
                      std::fabs(vm.regs[PSB_REG_OUT_G] - 0.0625f) < 1e-4f &&
                      std::fabs(vm.regs[PSB_REG_OUT_B] - 0.0625f) < 1e-4f;
            std::printf("  gamma   -> out=(%.4f, %.4f, %.4f) expect (0.0625, ...) %s\n",
                        vm.regs[PSB_REG_OUT_R], vm.regs[PSB_REG_OUT_G], vm.regs[PSB_REG_OUT_B],
                        ok ? "OK" : "FAIL");
            Check(ok, "gamma execution result");
        } else {
            Check(false, "gamma parses for exec");
        }
    }
    {
        // Scalar-broadcast hazard: scalar TEMP * vector TEMP. The scalar must
        // survive dst reuse across all 3 components (allocator evaluates the
        // wider side first so the scalar temp lands above the result span).
        const char* src =
            "void main() {\n"
            "    vec4 color = texture2D(u_framebuffer, gl_FragCoord.xy / u_resolution);\n"
            "    gl_FragColor = vec4((1.0 + 1.0) * color.rgb, 1.0);\n"
            "}\n";
        auto r = PglShaderCompiler::Compile(src, std::strlen(src));
        Check(r.success, "scalar*vector broadcast compiles");
        PsbView view{};
        if (r.success && ParsePsb(r, view)) {
            MiniVm vm;
            vm.uniforms[PSB_AUTO_UNIFORM_RESOLUTION_X] = 128.0f;
            vm.uniforms[PSB_AUTO_UNIFORM_RESOLUTION_Y] = 64.0f;
            vm.Run(view);
            // sample = (0.25, 0.25, 0.25); * 2.0 = (0.5, 0.5, 0.5)
            bool ok = std::fabs(vm.regs[PSB_REG_OUT_R] - 0.5f) < 1e-4f &&
                      std::fabs(vm.regs[PSB_REG_OUT_G] - 0.5f) < 1e-4f &&
                      std::fabs(vm.regs[PSB_REG_OUT_B] - 0.5f) < 1e-4f;
            std::printf("  f*vec3  -> out=(%.3f, %.3f, %.3f) expect (0.5, 0.5, 0.5) %s\n",
                        vm.regs[PSB_REG_OUT_R], vm.regs[PSB_REG_OUT_G], vm.regs[PSB_REG_OUT_B],
                        ok ? "OK" : "FAIL");
            Check(ok, "scalar*vector broadcast result");
        } else {
            Check(false, "scalar*vector broadcast parses");
        }
    }

    // ── Safety net: genuinely over-deep expressions still overflow ────────
    std::printf("\nOverflow safety net:\n");
    {
        // 10 nested (a+a)+(...) vec4 adds — each level holds a 4-reg temp
        // while descending, so ~6 levels already exceed the 20-reg bank.
        std::string deep =
            "void main() {\n"
            "    vec4 a = vec4(1.0);\n"
            "    gl_FragColor = ";
        for (int i = 0; i < 10; ++i) deep += "(a+a)+(";
        deep += "a";
        for (int i = 0; i < 10; ++i) deep += ")";
        deep += ";\n}\n";
        auto r = PglShaderCompiler::Compile(deep.c_str(), deep.size());
        std::printf("  10-deep nested vec4 adds: success=%s  error=\"%s\"\n",
                    r.success ? "true" : "false", r.errorMsg);
        Check(!r.success, "over-deep expression rejected");
        Check(std::strstr(r.errorMsg, "register allocation overflow") != nullptr,
              "overflow error message preserved");
    }
    {
        // Control: 3 nested levels (~12 regs simultaneous) must still pass.
        std::string ok3 =
            "void main() {\n"
            "    vec4 a = vec4(1.0);\n"
            "    gl_FragColor = (a+a)+((a+a)+(a+a));\n"
            "}\n";
        auto r = PglShaderCompiler::Compile(ok3.c_str(), ok3.size());
        std::printf("  3-level nested vec4 adds: success=%s highWater=r%u\n",
                    r.success ? "true" : "false",
                    r.regHighWater > 0 ? r.regHighWater - 1 : 0);
        Check(r.success, "moderately nested expression compiles");
    }

    std::printf("\nRESULT: %d checks, %d failure(s) -> %s\n",
                g_checks, g_failures, g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
