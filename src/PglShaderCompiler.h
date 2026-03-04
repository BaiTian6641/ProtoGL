/**
 * @file PglShaderCompiler.h
 * @brief PGLSL → PSB bytecode compiler (host-side, runs on ESP32-S3).
 *
 * Compiles a subset of GLSL ES 1.00 ("PGLSL") into PGL Shader Bytecode (PSB)
 * for execution on the RP2350 PglShaderVM.
 *
 * Phases:
 *   1. Lexer     — tokenize PGLSL source
 *   2. Parser    — build flat expression list (no control flow)
 *   3. Type check — resolve types, validate built-ins
 *   4. Register allocate — linear scan over 20 user registers (r8–r27)
 *   5. Code gen  — emit 4-byte PSB instructions
 *
 * Limitations matching the VM:
 *   - No control flow (if/for/while)
 *   - No user-defined functions (only void main())
 *   - Max 16 uniforms, 32 constants, 256 instructions, 32 registers
 *   - One texture sampler (u_framebuffer)
 *   - Types: float, vec2, vec3, vec4, sampler2D
 *
 * Usage:
 *   auto result = PglShaderCompiler::Compile(source, strlen(source));
 *   if (result.success) {
 *       encoder.CreateShaderProgram(0, result.bytecode, result.bytecodeSize);
 *   }
 *
 * ProtoGL API Specification v0.6
 */

#pragma once

#include "PglShaderBytecode.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>

class PglShaderCompiler {
public:
    struct CompileResult {
        bool     success;
        uint8_t  bytecode[PSB_MAX_PROGRAM_SIZE];
        uint16_t bytecodeSize;
        char     errorMsg[128];
        uint16_t errorLine;
    };

    /**
     * @brief Compile PGLSL source text into PSB bytecode.
     * @param source       Null-terminated PGLSL source string.
     * @param sourceLength Length of source (excluding null terminator).
     * @return CompileResult with bytecodeSize > 0 on success.
     */
    static CompileResult Compile(const char* source, size_t sourceLength) {
        PglShaderCompiler compiler;
        return compiler.DoCompile(source, sourceLength);
    }

private:
    // ─── Token Types ────────────────────────────────────────────────────
    enum TokenType {
        TOK_EOF, TOK_IDENT, TOK_NUMBER, TOK_PLUS, TOK_MINUS, TOK_STAR,
        TOK_SLASH, TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
        TOK_SEMICOLON, TOK_COMMA, TOK_DOT, TOK_EQUALS, TOK_HASH,
        // Keywords
        TOK_VOID, TOK_FLOAT, TOK_VEC2, TOK_VEC3, TOK_VEC4, TOK_INT,
        TOK_UNIFORM, TOK_PRECISION, TOK_MEDIUMP, TOK_HIGHP, TOK_LOWP,
        TOK_VERSION, TOK_SAMPLER2D, TOK_MAIN,
    };

    struct Token {
        TokenType   type;
        const char* start;
        uint16_t    length;
        uint16_t    line;
        float       numValue;  // For TOK_NUMBER
    };

    // ─── AST Node Types ─────────────────────────────────────────────────
    enum NodeType {
        NODE_LITERAL,        // float constant
        NODE_VARIABLE,       // named variable reference
        NODE_SWIZZLE,        // var.xy, var.rgb, etc.
        NODE_BINARY_OP,      // +, -, *, /
        NODE_UNARY_NEG,      // -expr
        NODE_FUNC_CALL,      // sin(), texture2D(), vec3(), etc.
        NODE_ASSIGN,         // var = expr
        NODE_COMPONENT_ASSIGN, // var.x = expr
        NODE_DECLARATION,    // float x = expr
    };

    // Simple value type tracking
    enum ValType : uint8_t {
        VTYPE_FLOAT  = 1,
        VTYPE_VEC2   = 2,
        VTYPE_VEC3   = 3,
        VTYPE_VEC4   = 4,
        VTYPE_SAMPLER = 5,
        VTYPE_VOID   = 0,
    };

    struct ASTNode {
        NodeType type;
        ValType  valType;

        // For literals
        float litValue;

        // For variables / identifiers
        char name[32];

        // For swizzle
        char swizzle[5]; // max "rgba" + null

        // For binary ops
        uint8_t op;  // '+', '-', '*', '/'

        // For function calls
        char funcName[32];
        uint8_t argCount;

        // Tree links (indices into node array)
        int16_t left;   // first child / LHS
        int16_t right;  // second child / RHS
        int16_t args[4]; // additional children for multi-arg functions
    };

    // ─── Compiler State ──────────────────────────────────────────────────

    static constexpr int MAX_TOKENS  = 2048;
    static constexpr int MAX_NODES   = 512;
    static constexpr int MAX_STMTS   = 128;
    static constexpr int MAX_VARS    = 48;
    static constexpr int MAX_INSTRS  = PSB_MAX_INSTRUCTIONS;

    Token   tokens_[MAX_TOKENS];
    int     tokenCount_ = 0;
    int     tokenPos_   = 0;

    ASTNode nodes_[MAX_NODES];
    int     nodeCount_ = 0;

    int16_t stmts_[MAX_STMTS];  // indices into nodes_
    int     stmtCount_ = 0;

    // Variable table (locals + built-ins + uniforms)
    struct VarInfo {
        char    name[32];
        ValType type;
        uint8_t reg;         // first register of this variable
        bool    isUniform;
        uint8_t uniformSlot; // for uniforms
    };
    VarInfo vars_[MAX_VARS];
    int     varCount_ = 0;

    // Uniform descriptors for output
    struct UniformInfo {
        char     name[32];
        ValType  type;
        uint8_t  slot;
        uint32_t nameHash;
    };
    UniformInfo uniforms_[PSB_MAX_UNIFORMS];
    int uniformCount_ = 0;

    // Constants pool
    float   constPool_[PSB_MAX_CONSTANTS];
    int     constCount_ = 0;

    // Instruction output
    PglShaderInstruction instrs_[MAX_INSTRS];
    int instrCount_ = 0;

    // Register allocator state
    uint8_t nextReg_ = PSB_REG_USER_START;
    bool    needsScratchCopy_ = false;

    // Error state
    char     errorMsg_[128];
    uint16_t errorLine_ = 0;
    bool     hasError_ = false;

    // ─── Main Compile Entry ──────────────────────────────────────────────

    CompileResult DoCompile(const char* source, size_t sourceLength) {
        CompileResult result{};
        result.success = false;
        result.bytecodeSize = 0;
        result.errorLine = 0;
        std::memset(result.errorMsg, 0, sizeof(result.errorMsg));
        std::memset(result.bytecode, 0, sizeof(result.bytecode));

        // Reset compiler state
        tokenCount_ = 0; tokenPos_ = 0;
        nodeCount_ = 0; stmtCount_ = 0; varCount_ = 0;
        uniformCount_ = 0; constCount_ = 0; instrCount_ = 0;
        nextReg_ = PSB_REG_USER_START;
        needsScratchCopy_ = false;
        hasError_ = false;
        errorLine_ = 0;
        std::memset(errorMsg_, 0, sizeof(errorMsg_));

        // Register built-in variables
        RegisterBuiltins();

        // Phase 1: Lex
        if (!Lex(source, sourceLength)) {
            SetError(result);
            return result;
        }

        // Phase 2: Parse
        if (!Parse()) {
            SetError(result);
            return result;
        }

        // Phase 3+4+5: Generate code (type check + regalloc + codegen combined)
        if (!GenerateCode()) {
            SetError(result);
            return result;
        }

        // Emit END instruction
        EmitInstruction(PSB_OP_END, PSB_OP_UNUSED, PSB_OP_UNUSED, PSB_OP_UNUSED);

        // Phase 6: Assemble PSB binary
        if (!AssembleBinary(result)) {
            SetError(result);
            return result;
        }

        result.success = true;
        return result;
    }

    void SetError(CompileResult& result) {
        result.success = false;
        std::memcpy(result.errorMsg, errorMsg_, sizeof(errorMsg_));
        result.errorLine = errorLine_;
    }

    void Error(uint16_t line, const char* msg) {
        if (hasError_) return;
        hasError_ = true;
        errorLine_ = line;
        snprintf(errorMsg_, sizeof(errorMsg_), "Line %u: %s", line, msg);
    }

    // ─── Built-in Variable Registration ──────────────────────────────────

    void RegisterBuiltins() {
        // gl_FragCoord → r0..r3
        AddVar("gl_FragCoord", VTYPE_VEC4, PSB_REG_FRAG_X, false, 0);

        // Current pixel color (input) → r4..r7
        // (accessible as a synthetic variable we'll handle in codegen)

        // gl_FragColor → r28..r31
        AddVar("gl_FragColor", VTYPE_VEC4, PSB_REG_OUT_R, false, 0);

        // u_resolution → auto-bound uniforms u0, u1
        AddVar("u_resolution", VTYPE_VEC2, 0, true, PSB_AUTO_UNIFORM_RESOLUTION_X);

        // u_time → auto-bound uniform u2
        AddVar("u_time", VTYPE_FLOAT, 0, true, PSB_AUTO_UNIFORM_TIME);

        // u_framebuffer → sampler (special, handled in TEX2D codegen)
        AddVar("u_framebuffer", VTYPE_SAMPLER, 0, false, 0);
    }

    void AddVar(const char* name, ValType type, uint8_t reg, bool isUniform, uint8_t uniSlot) {
        if (varCount_ >= MAX_VARS) return;
        VarInfo& v = vars_[varCount_++];
        strncpy(v.name, name, sizeof(v.name) - 1);
        v.name[sizeof(v.name) - 1] = '\0';
        v.type = type;
        v.reg = reg;
        v.isUniform = isUniform;
        v.uniformSlot = uniSlot;
    }

    VarInfo* FindVar(const char* name) {
        for (int i = 0; i < varCount_; ++i) {
            if (strcmp(vars_[i].name, name) == 0) return &vars_[i];
        }
        return nullptr;
    }

    // ─── Lexer ──────────────────────────────────────────────────────────

    bool Lex(const char* src, size_t len) {
        uint16_t line = 1;
        const char* p = src;
        const char* end = src + len;

        while (p < end) {
            // Skip whitespace
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
            if (p >= end) break;

            if (*p == '\n') { ++line; ++p; continue; }

            // Skip line comments
            if (p + 1 < end && *p == '/' && *(p+1) == '/') {
                while (p < end && *p != '\n') ++p;
                continue;
            }

            // Skip block comments
            if (p + 1 < end && *p == '/' && *(p+1) == '*') {
                p += 2;
                while (p + 1 < end && !(*p == '*' && *(p+1) == '/')) {
                    if (*p == '\n') ++line;
                    ++p;
                }
                if (p + 1 < end) p += 2;
                continue;
            }

            Token tok{};
            tok.line = line;
            tok.start = p;

            // Number (float literal)
            if (*p >= '0' && *p <= '9') {
                const char* numStart = p;
                while (p < end && ((*p >= '0' && *p <= '9') || *p == '.')) ++p;
                // Handle 'f' suffix
                if (p < end && *p == 'f') ++p;
                tok.type = TOK_NUMBER;
                tok.length = static_cast<uint16_t>(p - numStart);
                // Parse the number
                char buf[32] = {};
                int copyLen = tok.length < 31 ? tok.length : 31;
                std::memcpy(buf, numStart, copyLen);
                if (buf[copyLen - 1] == 'f') buf[copyLen - 1] = '\0';
                tok.numValue = static_cast<float>(atof(buf));
                AddToken(tok);
                continue;
            }

            // Identifier or keyword
            if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
                const char* idStart = p;
                while (p < end && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                       (*p >= '0' && *p <= '9') || *p == '_')) ++p;
                tok.length = static_cast<uint16_t>(p - idStart);
                tok.type = ClassifyKeyword(idStart, tok.length);
                AddToken(tok);
                continue;
            }

            // Single-character tokens
            tok.length = 1;
            switch (*p) {
                case '+': tok.type = TOK_PLUS;      break;
                case '-': tok.type = TOK_MINUS;      break;
                case '*': tok.type = TOK_STAR;       break;
                case '/': tok.type = TOK_SLASH;      break;
                case '(': tok.type = TOK_LPAREN;     break;
                case ')': tok.type = TOK_RPAREN;     break;
                case '{': tok.type = TOK_LBRACE;     break;
                case '}': tok.type = TOK_RBRACE;     break;
                case ';': tok.type = TOK_SEMICOLON;  break;
                case ',': tok.type = TOK_COMMA;      break;
                case '.': tok.type = TOK_DOT;        break;
                case '=': tok.type = TOK_EQUALS;     break;
                case '#': tok.type = TOK_HASH;       break;
                default:
                    // skip unknown character
                    ++p;
                    continue;
            }
            ++p;
            AddToken(tok);
        }

        // Add EOF
        Token eof{};
        eof.type = TOK_EOF;
        eof.line = line;
        eof.start = p;
        eof.length = 0;
        AddToken(eof);

        return true;
    }

    void AddToken(const Token& tok) {
        if (tokenCount_ < MAX_TOKENS) {
            tokens_[tokenCount_++] = tok;
        }
    }

    TokenType ClassifyKeyword(const char* s, uint16_t len) {
        if (MatchStr(s, len, "void"))      return TOK_VOID;
        if (MatchStr(s, len, "float"))     return TOK_FLOAT;
        if (MatchStr(s, len, "vec2"))      return TOK_VEC2;
        if (MatchStr(s, len, "vec3"))      return TOK_VEC3;
        if (MatchStr(s, len, "vec4"))      return TOK_VEC4;
        if (MatchStr(s, len, "int"))       return TOK_INT;
        if (MatchStr(s, len, "uniform"))   return TOK_UNIFORM;
        if (MatchStr(s, len, "precision")) return TOK_PRECISION;
        if (MatchStr(s, len, "mediump"))   return TOK_MEDIUMP;
        if (MatchStr(s, len, "highp"))     return TOK_HIGHP;
        if (MatchStr(s, len, "lowp"))      return TOK_LOWP;
        if (MatchStr(s, len, "sampler2D")) return TOK_SAMPLER2D;
        if (MatchStr(s, len, "main"))      return TOK_MAIN;
        return TOK_IDENT;
    }

    static bool MatchStr(const char* s, uint16_t len, const char* keyword) {
        size_t kLen = strlen(keyword);
        return len == kLen && std::memcmp(s, keyword, kLen) == 0;
    }

    // ─── Parser Helpers ─────────────────────────────────────────────────

    Token& Peek() { return tokens_[tokenPos_ < tokenCount_ ? tokenPos_ : tokenCount_ - 1]; }

    Token& Advance() {
        Token& t = Peek();
        if (tokenPos_ < tokenCount_ - 1) ++tokenPos_;
        return t;
    }

    bool Check(TokenType t) { return Peek().type == t; }

    bool Match(TokenType t) {
        if (Check(t)) { Advance(); return true; }
        return false;
    }

    bool Expect(TokenType t) {
        if (Check(t)) { Advance(); return true; }
        Error(Peek().line, "unexpected token");
        return false;
    }

    void TokenToStr(const Token& tok, char* buf, int maxLen) {
        int len = tok.length < maxLen - 1 ? tok.length : maxLen - 1;
        std::memcpy(buf, tok.start, len);
        buf[len] = '\0';
    }

    // ─── Parser ─────────────────────────────────────────────────────────

    bool Parse() {
        // Skip #version directive
        if (Check(TOK_HASH)) {
            while (!Check(TOK_EOF) && Peek().line == tokens_[tokenPos_].line) {
                Advance();
            }
        }

        // Parse top-level declarations
        while (!Check(TOK_EOF) && !hasError_) {
            // Skip precision qualifiers
            if (Check(TOK_PRECISION)) {
                SkipToSemicolon();
                continue;
            }

            // #version line
            if (Check(TOK_HASH)) {
                uint16_t curLine = Peek().line;
                while (!Check(TOK_EOF) && Peek().line == curLine) Advance();
                continue;
            }

            // Uniform declaration
            if (Check(TOK_UNIFORM)) {
                ParseUniform();
                continue;
            }

            // void main() { ... }
            if (Check(TOK_VOID)) {
                ParseMainFunction();
                break;
            }

            // Unknown top-level — skip to semicolon or brace
            Advance();
        }

        return !hasError_;
    }

    void SkipToSemicolon() {
        while (!Check(TOK_EOF) && !Check(TOK_SEMICOLON)) Advance();
        if (Check(TOK_SEMICOLON)) Advance();
    }

    void ParseUniform() {
        Advance(); // skip 'uniform'

        ValType uType = ParseType();
        if (uType == VTYPE_VOID) {
            Error(Peek().line, "invalid uniform type");
            return;
        }

        if (!Check(TOK_IDENT)) {
            Error(Peek().line, "expected uniform name");
            return;
        }

        Token& nameTok = Advance();
        char name[32];
        TokenToStr(nameTok, name, sizeof(name));

        Expect(TOK_SEMICOLON);

        // Check if this is an auto-bound uniform
        VarInfo* existing = FindVar(name);
        if (existing && existing->isUniform) {
            // Already registered as auto-bound; skip re-registration.
            return;
        }

        // Register as user uniform
        if (uniformCount_ >= PSB_MAX_UNIFORMS) {
            Error(nameTok.line, "too many uniforms");
            return;
        }

        uint8_t uniSlot = PSB_USER_UNIFORM_START + static_cast<uint8_t>(uniformCount_);

        // Allocate register(s) for the uniform
        uint8_t compCount = static_cast<uint8_t>(uType);
        uint8_t reg = AllocRegs(compCount);

        AddVar(name, uType, reg, true, uniSlot);

        UniformInfo& ui = uniforms_[uniformCount_++];
        strncpy(ui.name, name, sizeof(ui.name) - 1);
        ui.name[sizeof(ui.name) - 1] = '\0';
        ui.type = uType;
        ui.slot = uniSlot;
        ui.nameHash = PsbFnv1a(name);
    }

    ValType ParseType() {
        if (Match(TOK_FLOAT))    return VTYPE_FLOAT;
        if (Match(TOK_VEC2))     return VTYPE_VEC2;
        if (Match(TOK_VEC3))     return VTYPE_VEC3;
        if (Match(TOK_VEC4))     return VTYPE_VEC4;
        if (Match(TOK_INT))      return VTYPE_FLOAT; // int treated as float
        if (Match(TOK_SAMPLER2D)) return VTYPE_SAMPLER;
        return VTYPE_VOID;
    }

    void ParseMainFunction() {
        Advance(); // void
        Expect(TOK_MAIN);
        Expect(TOK_LPAREN);
        Expect(TOK_RPAREN);
        Expect(TOK_LBRACE);

        // Parse statements until closing brace
        while (!Check(TOK_RBRACE) && !Check(TOK_EOF) && !hasError_) {
            int16_t node = ParseStatement();
            if (node >= 0 && stmtCount_ < MAX_STMTS) {
                stmts_[stmtCount_++] = node;
            }
        }

        Expect(TOK_RBRACE);
    }

    int16_t ParseStatement() {
        // Type declaration: float x = expr; / vec2 uv = expr; etc.
        if (Check(TOK_FLOAT) || Check(TOK_VEC2) || Check(TOK_VEC3) || Check(TOK_VEC4) || Check(TOK_INT)) {
            return ParseDeclaration();
        }

        // Assignment or expression statement
        int16_t expr = ParseExpression();
        if (expr < 0) {
            SkipToSemicolon();
            return -1;
        }

        Expect(TOK_SEMICOLON);
        return expr;
    }

    int16_t ParseDeclaration() {
        ValType declType = ParseType();

        if (!Check(TOK_IDENT)) {
            Error(Peek().line, "expected variable name");
            SkipToSemicolon();
            return -1;
        }

        Token& nameTok = Advance();
        char name[32];
        TokenToStr(nameTok, name, sizeof(name));

        // Allocate registers
        uint8_t compCount = static_cast<uint8_t>(declType);
        uint8_t reg = AllocRegs(compCount);
        AddVar(name, declType, reg, false, 0);

        int16_t initExpr = -1;
        if (Match(TOK_EQUALS)) {
            initExpr = ParseExpression();
        }

        Expect(TOK_SEMICOLON);

        // Create declaration node
        int16_t nodeIdx = AllocNode();
        if (nodeIdx < 0) return -1;
        ASTNode& n = nodes_[nodeIdx];
        n.type = NODE_DECLARATION;
        n.valType = declType;
        strncpy(n.name, name, sizeof(n.name) - 1);
        n.left = initExpr;
        return nodeIdx;
    }

    int16_t ParseExpression() {
        return ParseAssignment();
    }

    int16_t ParseAssignment() {
        int16_t left = ParseAddSub();
        if (left < 0) return -1;

        if (Check(TOK_EQUALS)) {
            Advance();
            int16_t right = ParseExpression();

            int16_t assignNode = AllocNode();
            if (assignNode < 0) return -1;

            ASTNode& n = nodes_[assignNode];
            // Check if left is a swizzle — that's a component assignment
            if (left >= 0 && nodes_[left].type == NODE_SWIZZLE) {
                n.type = NODE_COMPONENT_ASSIGN;
            } else {
                n.type = NODE_ASSIGN;
            }
            n.left = left;
            n.right = right;
            return assignNode;
        }

        return left;
    }

    int16_t ParseAddSub() {
        int16_t left = ParseMulDiv();
        if (left < 0) return -1;

        while (Check(TOK_PLUS) || Check(TOK_MINUS)) {
            uint8_t op = Check(TOK_PLUS) ? '+' : '-';
            Advance();
            int16_t right = ParseMulDiv();
            if (right < 0) return -1;

            int16_t binNode = AllocNode();
            if (binNode < 0) return -1;
            ASTNode& n = nodes_[binNode];
            n.type = NODE_BINARY_OP;
            n.op = op;
            n.left = left;
            n.right = right;
            // Type is wider of left/right
            n.valType = WiderType(NodeValType(left), NodeValType(right));
            left = binNode;
        }

        return left;
    }

    int16_t ParseMulDiv() {
        int16_t left = ParseUnary();
        if (left < 0) return -1;

        while (Check(TOK_STAR) || Check(TOK_SLASH)) {
            uint8_t op = Check(TOK_STAR) ? '*' : '/';
            Advance();
            int16_t right = ParseUnary();
            if (right < 0) return -1;

            int16_t binNode = AllocNode();
            if (binNode < 0) return -1;
            ASTNode& n = nodes_[binNode];
            n.type = NODE_BINARY_OP;
            n.op = op;
            n.left = left;
            n.right = right;
            n.valType = WiderType(NodeValType(left), NodeValType(right));
            left = binNode;
        }

        return left;
    }

    int16_t ParseUnary() {
        if (Check(TOK_MINUS)) {
            Advance();
            int16_t operand = ParseUnary();
            if (operand < 0) return -1;

            int16_t negNode = AllocNode();
            if (negNode < 0) return -1;
            ASTNode& n = nodes_[negNode];
            n.type = NODE_UNARY_NEG;
            n.left = operand;
            n.valType = NodeValType(operand);
            return negNode;
        }

        return ParsePostfix();
    }

    int16_t ParsePostfix() {
        int16_t primary = ParsePrimary();
        if (primary < 0) return -1;

        // Handle .xyz swizzle
        while (Check(TOK_DOT)) {
            Advance(); // skip '.'
            if (!Check(TOK_IDENT)) {
                Error(Peek().line, "expected swizzle after '.'");
                return -1;
            }

            Token& swzTok = Advance();
            char swz[5] = {};
            int swzLen = swzTok.length < 4 ? swzTok.length : 4;
            std::memcpy(swz, swzTok.start, swzLen);

            int16_t swzNode = AllocNode();
            if (swzNode < 0) return -1;
            ASTNode& n = nodes_[swzNode];
            n.type = NODE_SWIZZLE;
            std::memcpy(n.swizzle, swz, 5);
            n.left = primary;
            n.valType = static_cast<ValType>(swzLen);
            primary = swzNode;
        }

        return primary;
    }

    int16_t ParsePrimary() {
        // Number literal
        if (Check(TOK_NUMBER)) {
            Token& num = Advance();
            int16_t litNode = AllocNode();
            if (litNode < 0) return -1;
            ASTNode& n = nodes_[litNode];
            n.type = NODE_LITERAL;
            n.valType = VTYPE_FLOAT;
            n.litValue = num.numValue;
            return litNode;
        }

        // Parenthesized expression
        if (Check(TOK_LPAREN)) {
            Advance();
            int16_t inner = ParseExpression();
            Expect(TOK_RPAREN);
            return inner;
        }

        // Type constructor or function call or variable
        if (Check(TOK_IDENT) || Check(TOK_VEC2) || Check(TOK_VEC3) || Check(TOK_VEC4) ||
            Check(TOK_FLOAT) || Check(TOK_MAIN)) {
            Token& idTok = Advance();
            char name[32];
            TokenToStr(idTok, name, sizeof(name));

            // Constructor / function call
            if (Check(TOK_LPAREN)) {
                return ParseFuncCall(name, idTok.line);
            }

            // Variable reference
            int16_t varNode = AllocNode();
            if (varNode < 0) return -1;
            ASTNode& n = nodes_[varNode];
            n.type = NODE_VARIABLE;
            strncpy(n.name, name, sizeof(n.name) - 1);

            VarInfo* v = FindVar(name);
            if (v) {
                n.valType = v->type;
            } else {
                n.valType = VTYPE_FLOAT; // assume float for unknown
            }

            return varNode;
        }

        Error(Peek().line, "unexpected token in expression");
        return -1;
    }

    int16_t ParseFuncCall(const char* name, uint16_t line) {
        Advance(); // skip '('

        int16_t funcNode = AllocNode();
        if (funcNode < 0) return -1;
        ASTNode& fn = nodes_[funcNode];
        fn.type = NODE_FUNC_CALL;
        strncpy(fn.funcName, name, sizeof(fn.funcName) - 1);
        fn.argCount = 0;

        // Parse arguments
        if (!Check(TOK_RPAREN)) {
            fn.args[0] = ParseExpression();
            fn.argCount = 1;
            fn.left = fn.args[0];

            while (Match(TOK_COMMA) && fn.argCount < 4) {
                fn.args[fn.argCount] = ParseExpression();
                if (fn.argCount == 1) fn.right = fn.args[1];
                fn.argCount++;
            }
        }

        Expect(TOK_RPAREN);

        // Determine return type
        fn.valType = GetFuncReturnType(name, fn.argCount);

        return funcNode;
    }

    ValType GetFuncReturnType(const char* name, uint8_t argCount) {
        // Type constructors
        if (strcmp(name, "vec2") == 0) return VTYPE_VEC2;
        if (strcmp(name, "vec3") == 0) return VTYPE_VEC3;
        if (strcmp(name, "vec4") == 0) return VTYPE_VEC4;
        if (strcmp(name, "float") == 0) return VTYPE_FLOAT;

        // Texture sampling
        if (strcmp(name, "texture2D") == 0) return VTYPE_VEC4;

        // Geometric functions returning scalar
        if (strcmp(name, "length") == 0)    return VTYPE_FLOAT;
        if (strcmp(name, "distance") == 0)  return VTYPE_FLOAT;
        if (strcmp(name, "dot") == 0)       return VTYPE_FLOAT;

        // Functions returning same type as input
        if (strcmp(name, "normalize") == 0) return VTYPE_VEC3; // assume vec3 for now
        if (strcmp(name, "cross") == 0)     return VTYPE_VEC3;
        if (strcmp(name, "reflect") == 0)   return VTYPE_VEC3;

        // Scalar math functions
        if (strcmp(name, "sin") == 0 || strcmp(name, "cos") == 0 ||
            strcmp(name, "tan") == 0 || strcmp(name, "asin") == 0 ||
            strcmp(name, "acos") == 0 || strcmp(name, "atan") == 0 ||
            strcmp(name, "pow") == 0 || strcmp(name, "exp") == 0 ||
            strcmp(name, "log") == 0 || strcmp(name, "exp2") == 0 ||
            strcmp(name, "log2") == 0 || strcmp(name, "sqrt") == 0 ||
            strcmp(name, "inversesqrt") == 0 || strcmp(name, "abs") == 0 ||
            strcmp(name, "sign") == 0 || strcmp(name, "floor") == 0 ||
            strcmp(name, "ceil") == 0 || strcmp(name, "fract") == 0 ||
            strcmp(name, "mod") == 0 || strcmp(name, "min") == 0 ||
            strcmp(name, "max") == 0 || strcmp(name, "clamp") == 0 ||
            strcmp(name, "mix") == 0 || strcmp(name, "step") == 0 ||
            strcmp(name, "smoothstep") == 0) {
            return VTYPE_FLOAT;
        }

        return VTYPE_FLOAT; // default
    }

    // ─── AST Allocation ─────────────────────────────────────────────────

    int16_t AllocNode() {
        if (nodeCount_ >= MAX_NODES) {
            Error(Peek().line, "AST node limit exceeded");
            return -1;
        }
        int16_t idx = static_cast<int16_t>(nodeCount_++);
        std::memset(&nodes_[idx], 0, sizeof(ASTNode));
        nodes_[idx].left = -1;
        nodes_[idx].right = -1;
        for (int i = 0; i < 4; ++i) nodes_[idx].args[i] = -1;
        return idx;
    }

    ValType NodeValType(int16_t nodeIdx) {
        if (nodeIdx < 0 || nodeIdx >= nodeCount_) return VTYPE_FLOAT;
        return nodes_[nodeIdx].valType;
    }

    ValType WiderType(ValType a, ValType b) {
        return static_cast<ValType>(a > b ? a : b);
    }

    // ─── Register Allocator ─────────────────────────────────────────────

    uint8_t AllocRegs(uint8_t count) {
        if (nextReg_ + count > PSB_REG_USER_END + 1) {
            // Overflowed user registers — wrap (will produce bad code, but error already set)
            Error(0, "register allocation overflow");
            return PSB_REG_USER_START;
        }
        uint8_t r = nextReg_;
        nextReg_ += count;
        return r;
    }

    // ─── Code Generation ────────────────────────────────────────────────

    bool GenerateCode() {
        // First, emit uniform load instructions for user uniforms
        for (int i = 0; i < uniformCount_; ++i) {
            UniformInfo& ui = uniforms_[i];
            VarInfo* v = FindVar(ui.name);
            if (!v) continue;

            uint8_t compCount = static_cast<uint8_t>(ui.type);
            for (uint8_t c = 0; c < compCount; ++c) {
                EmitInstruction(PSB_OP_LUNI, v->reg + c, ui.slot + c, PSB_OP_UNUSED);
            }
        }

        // Generate code for each statement
        for (int i = 0; i < stmtCount_ && !hasError_; ++i) {
            EmitNode(stmts_[i]);
        }

        return !hasError_;
    }

    // Returns the register (or first register of a multi-component result) where
    // the value of this node is stored after codegen.
    uint8_t EmitNode(int16_t nodeIdx) {
        if (nodeIdx < 0 || hasError_) return PSB_OP_UNUSED;

        ASTNode& n = nodes_[nodeIdx];

        switch (n.type) {
            case NODE_LITERAL:
                return EmitLiteral(n);

            case NODE_VARIABLE:
                return EmitVariable(n);

            case NODE_SWIZZLE:
                return EmitSwizzle(n);

            case NODE_BINARY_OP:
                return EmitBinaryOp(n);

            case NODE_UNARY_NEG:
                return EmitUnaryNeg(n);

            case NODE_FUNC_CALL:
                return EmitFuncCall(n);

            case NODE_ASSIGN:
                return EmitAssignment(n);

            case NODE_COMPONENT_ASSIGN:
                return EmitComponentAssign(n);

            case NODE_DECLARATION:
                return EmitDeclaration(n);

            default:
                return PSB_OP_UNUSED;
        }
    }

    uint8_t EmitLiteral(ASTNode& n) {
        // Check if it matches a built-in literal
        for (uint8_t i = 0; i < PSB_LITERAL_COUNT; ++i) {
            if (n.litValue == PSB_LITERALS[i]) {
                return PSB_OP_LITERAL_BASE + i;
            }
        }

        // Add to constant pool
        uint8_t cIdx = AddConstant(n.litValue);
        uint8_t dst = AllocRegs(1);
        EmitInstruction(PSB_OP_LCONST, dst, cIdx, PSB_OP_UNUSED);
        return dst;
    }

    uint8_t EmitVariable(ASTNode& n) {
        VarInfo* v = FindVar(n.name);
        if (!v) {
            Error(0, "undefined variable");
            return PSB_OP_UNUSED;
        }

        if (v->isUniform) {
            // Uniform — operand encoding uses uniform range
            return PSB_OP_UNIFORM_BASE + v->uniformSlot;
        }

        return v->reg;
    }

    uint8_t EmitSwizzle(ASTNode& n) {
        uint8_t baseReg = EmitNode(n.left);
        if (baseReg == PSB_OP_UNUSED) return PSB_OP_UNUSED;

        // Resolve base register from operand encoding
        uint8_t realBase = baseReg;
        if (baseReg >= PSB_OP_UNIFORM_BASE && baseReg <= PSB_OP_UNIFORM_END) {
            // Uniforms — need to load first
            uint8_t uSlot = baseReg - PSB_OP_UNIFORM_BASE;
            uint8_t components = strlen(n.swizzle);
            uint8_t dst = AllocRegs(components);
            for (uint8_t i = 0; i < components; ++i) {
                uint8_t offset = SwizzleOffset(n.swizzle[i]);
                EmitInstruction(PSB_OP_LUNI, dst + i, uSlot + offset, PSB_OP_UNUSED);
            }
            return dst;
        }

        // Register — compute offsets directly
        uint8_t swzLen = static_cast<uint8_t>(strlen(n.swizzle));
        if (swzLen == 1) {
            return realBase + SwizzleOffset(n.swizzle[0]);
        }

        // Multi-component swizzle — emit MOVs into temp registers
        uint8_t dst = AllocRegs(swzLen);
        for (uint8_t i = 0; i < swzLen; ++i) {
            uint8_t srcReg = realBase + SwizzleOffset(n.swizzle[i]);
            EmitInstruction(PSB_OP_MOV, dst + i, srcReg, PSB_OP_UNUSED);
        }
        return dst;
    }

    static uint8_t SwizzleOffset(char c) {
        switch (c) {
            case 'x': case 'r': case 's': return 0;
            case 'y': case 'g': case 't': return 1;
            case 'z': case 'b': case 'p': return 2;
            case 'w': case 'a': case 'q': return 3;
            default: return 0;
        }
    }

    uint8_t EmitBinaryOp(ASTNode& n) {
        uint8_t left = EmitNode(n.left);
        uint8_t right = EmitNode(n.right);

        uint8_t opcode;
        switch (n.op) {
            case '+': opcode = PSB_OP_ADD; break;
            case '-': opcode = PSB_OP_SUB; break;
            case '*': opcode = PSB_OP_MUL; break;
            case '/': opcode = PSB_OP_DIV; break;
            default:  opcode = PSB_OP_ADD; break;
        }

        // For vector types, emit component-wise operations
        uint8_t compCount = static_cast<uint8_t>(n.valType);
        if (compCount <= 1) compCount = 1;

        uint8_t dst = AllocRegs(compCount);
        for (uint8_t c = 0; c < compCount; ++c) {
            uint8_t lOp = OperandOffset(left, c, NodeValType(n.left));
            uint8_t rOp = OperandOffset(right, c, NodeValType(n.right));
            EmitInstruction(opcode, dst + c, lOp, rOp);
        }
        return dst;
    }

    uint8_t EmitUnaryNeg(ASTNode& n) {
        uint8_t operand = EmitNode(n.left);
        uint8_t compCount = static_cast<uint8_t>(n.valType);
        if (compCount <= 1) compCount = 1;

        uint8_t dst = AllocRegs(compCount);
        for (uint8_t c = 0; c < compCount; ++c) {
            uint8_t srcOp = OperandOffset(operand, c, NodeValType(n.left));
            EmitInstruction(PSB_OP_NEG, dst + c, srcOp, PSB_OP_UNUSED);
        }
        return dst;
    }

    uint8_t EmitFuncCall(ASTNode& n) {
        const char* fname = n.funcName;

        // ── Type constructors ───────────────────────────────────────────
        if (strcmp(fname, "vec2") == 0) return EmitConstructor(n, 2);
        if (strcmp(fname, "vec3") == 0) return EmitConstructor(n, 3);
        if (strcmp(fname, "vec4") == 0) return EmitConstructor(n, 4);
        if (strcmp(fname, "float") == 0) {
            if (n.argCount > 0 && n.args[0] >= 0) return EmitNode(n.args[0]);
            return PSB_OP_LITERAL_BASE; // 0.0
        }

        // ── texture2D ───────────────────────────────────────────────────
        if (strcmp(fname, "texture2D") == 0) {
            needsScratchCopy_ = true;
            // arg0 = sampler (ignored, always u_framebuffer)
            // arg1 = vec2 uv
            uint8_t uvReg = (n.argCount >= 2 && n.args[1] >= 0)
                            ? EmitNode(n.args[1]) : PSB_OP_LITERAL_BASE;
            uint8_t dst = AllocRegs(4);
            EmitInstruction(PSB_OP_TEX2D, dst, uvReg, PSB_OP_UNUSED);
            return dst;
        }

        // ── Single-arg math functions ───────────────────────────────────
        uint8_t mathOp = GetMathOpcode(fname);
        if (mathOp != 0) {
            uint8_t arg = (n.argCount > 0 && n.args[0] >= 0) ? EmitNode(n.args[0]) : PSB_OP_LITERAL_BASE;
            uint8_t dst = AllocRegs(1);

            // Two-arg math functions
            if (strcmp(fname, "pow") == 0 || strcmp(fname, "atan") == 0 ||
                strcmp(fname, "mod") == 0 || strcmp(fname, "min") == 0 ||
                strcmp(fname, "max") == 0) {
                uint8_t arg2 = (n.argCount > 1 && n.args[1] >= 0) ? EmitNode(n.args[1]) : PSB_OP_LITERAL_BASE;
                if (mathOp == PSB_OP_ATAN && n.argCount == 2) mathOp = PSB_OP_ATAN2;
                EmitInstruction(mathOp, dst, arg, arg2);
                return dst;
            }

            // Three-arg functions
            if (strcmp(fname, "clamp") == 0 || strcmp(fname, "mix") == 0 ||
                strcmp(fname, "smoothstep") == 0) {
                uint8_t arg2 = (n.argCount > 1 && n.args[1] >= 0) ? EmitNode(n.args[1]) : PSB_OP_LITERAL_BASE;
                uint8_t arg3 = (n.argCount > 2 && n.args[2] >= 0) ? EmitNode(n.args[2]) : PSB_OP_LITERAL_BASE;
                // For 3-operand instructions: dst is pre-loaded with 3rd arg
                EmitInstruction(PSB_OP_MOV, dst, arg3, PSB_OP_UNUSED);
                EmitInstruction(mathOp, dst, arg, arg2);
                return dst;
            }

            // step(edge, x) — 2 args
            if (strcmp(fname, "step") == 0) {
                uint8_t arg2 = (n.argCount > 1 && n.args[1] >= 0) ? EmitNode(n.args[1]) : PSB_OP_LITERAL_BASE;
                EmitInstruction(PSB_OP_STEP, dst, arg, arg2);
                return dst;
            }

            // Single-arg
            EmitInstruction(mathOp, dst, arg, PSB_OP_UNUSED);
            return dst;
        }

        // ── Geometric functions ─────────────────────────────────────────
        if (strcmp(fname, "length") == 0) {
            uint8_t arg = (n.argCount > 0 && n.args[0] >= 0) ? EmitNode(n.args[0]) : PSB_OP_LITERAL_BASE;
            uint8_t dst = AllocRegs(1);
            ValType argType = (n.argCount > 0 && n.args[0] >= 0) ? NodeValType(n.args[0]) : VTYPE_FLOAT;
            EmitInstruction(argType >= VTYPE_VEC3 ? PSB_OP_LEN3 : PSB_OP_LEN2,
                            dst, arg, PSB_OP_UNUSED);
            return dst;
        }

        if (strcmp(fname, "distance") == 0) {
            uint8_t arg1 = (n.argCount > 0 && n.args[0] >= 0) ? EmitNode(n.args[0]) : PSB_OP_LITERAL_BASE;
            uint8_t arg2 = (n.argCount > 1 && n.args[1] >= 0) ? EmitNode(n.args[1]) : PSB_OP_LITERAL_BASE;
            uint8_t dst = AllocRegs(1);
            EmitInstruction(PSB_OP_DIST2, dst, arg1, arg2);
            return dst;
        }

        if (strcmp(fname, "dot") == 0) {
            uint8_t arg1 = (n.argCount > 0 && n.args[0] >= 0) ? EmitNode(n.args[0]) : PSB_OP_LITERAL_BASE;
            uint8_t arg2 = (n.argCount > 1 && n.args[1] >= 0) ? EmitNode(n.args[1]) : PSB_OP_LITERAL_BASE;
            uint8_t dst = AllocRegs(1);
            ValType argType = (n.argCount > 0 && n.args[0] >= 0) ? NodeValType(n.args[0]) : VTYPE_VEC2;
            EmitInstruction(argType >= VTYPE_VEC3 ? PSB_OP_DOT3 : PSB_OP_DOT2,
                            dst, arg1, arg2);
            return dst;
        }

        if (strcmp(fname, "normalize") == 0) {
            uint8_t arg = (n.argCount > 0 && n.args[0] >= 0) ? EmitNode(n.args[0]) : PSB_OP_LITERAL_BASE;
            ValType argType = (n.argCount > 0 && n.args[0] >= 0) ? NodeValType(n.args[0]) : VTYPE_VEC3;
            uint8_t comps = (argType >= VTYPE_VEC3) ? 3 : 2;
            uint8_t dst = AllocRegs(comps);
            EmitInstruction(comps == 3 ? PSB_OP_NORM3 : PSB_OP_NORM2,
                            dst, arg, PSB_OP_UNUSED);
            return dst;
        }

        if (strcmp(fname, "cross") == 0) {
            uint8_t arg1 = (n.argCount > 0 && n.args[0] >= 0) ? EmitNode(n.args[0]) : PSB_OP_LITERAL_BASE;
            uint8_t arg2 = (n.argCount > 1 && n.args[1] >= 0) ? EmitNode(n.args[1]) : PSB_OP_LITERAL_BASE;
            uint8_t dst = AllocRegs(3);
            EmitInstruction(PSB_OP_CROSS, dst, arg1, arg2);
            return dst;
        }

        Error(0, "unknown function");
        return PSB_OP_UNUSED;
    }

    uint8_t EmitConstructor(ASTNode& n, uint8_t targetComps) {
        uint8_t dst = AllocRegs(targetComps);
        uint8_t filled = 0;

        for (uint8_t i = 0; i < n.argCount && filled < targetComps; ++i) {
            if (n.args[i] < 0) continue;
            uint8_t argReg = EmitNode(n.args[i]);
            ValType aType = NodeValType(n.args[i]);
            uint8_t argComps = static_cast<uint8_t>(aType);
            if (argComps < 1) argComps = 1;

            for (uint8_t c = 0; c < argComps && filled < targetComps; ++c) {
                uint8_t srcOp = OperandOffset(argReg, c, aType);
                EmitInstruction(PSB_OP_MOV, dst + filled, srcOp, PSB_OP_UNUSED);
                filled++;
            }
        }

        // If only 1 arg provided and target needs more (e.g. vec3(0.5)),
        // broadcast the single value
        if (n.argCount == 1 && filled < targetComps) {
            uint8_t srcOp = dst; // first component
            for (; filled < targetComps; ++filled) {
                EmitInstruction(PSB_OP_MOV, dst + filled, srcOp, PSB_OP_UNUSED);
            }
        }

        return dst;
    }

    uint8_t EmitAssignment(ASTNode& n) {
        uint8_t valueReg = EmitNode(n.right);

        // Get target variable
        if (n.left < 0) return PSB_OP_UNUSED;
        ASTNode& target = nodes_[n.left];

        VarInfo* v = FindVar(target.name);
        if (!v) {
            Error(0, "undefined assignment target");
            return PSB_OP_UNUSED;
        }

        uint8_t compCount = static_cast<uint8_t>(v->type);
        if (compCount < 1) compCount = 1;

        for (uint8_t c = 0; c < compCount; ++c) {
            uint8_t srcOp = OperandOffset(valueReg, c, NodeValType(n.right));
            EmitInstruction(PSB_OP_MOV, v->reg + c, srcOp, PSB_OP_UNUSED);
        }

        return v->reg;
    }

    uint8_t EmitComponentAssign(ASTNode& n) {
        // n.left is a NODE_SWIZZLE, n.right is the value expression
        uint8_t valueReg = EmitNode(n.right);

        if (n.left < 0) return PSB_OP_UNUSED;
        ASTNode& swzNode = nodes_[n.left];
        if (swzNode.left < 0) return PSB_OP_UNUSED;

        // Get the base variable from the swizzle's child
        ASTNode& baseNode = nodes_[swzNode.left];
        VarInfo* v = FindVar(baseNode.name);
        if (!v) {
            Error(0, "undefined assignment target");
            return PSB_OP_UNUSED;
        }

        uint8_t swzLen = static_cast<uint8_t>(strlen(swzNode.swizzle));
        for (uint8_t i = 0; i < swzLen; ++i) {
            uint8_t offset = SwizzleOffset(swzNode.swizzle[i]);
            uint8_t srcOp = OperandOffset(valueReg, i, NodeValType(n.right));
            EmitInstruction(PSB_OP_MOV, v->reg + offset, srcOp, PSB_OP_UNUSED);
        }

        return v->reg;
    }

    uint8_t EmitDeclaration(ASTNode& n) {
        VarInfo* v = FindVar(n.name);
        if (!v) return PSB_OP_UNUSED;

        if (n.left >= 0) {
            // Has initializer
            uint8_t initReg = EmitNode(n.left);
            uint8_t compCount = static_cast<uint8_t>(v->type);
            if (compCount < 1) compCount = 1;

            for (uint8_t c = 0; c < compCount; ++c) {
                uint8_t srcOp = OperandOffset(initReg, c, NodeValType(n.left));
                EmitInstruction(PSB_OP_MOV, v->reg + c, srcOp, PSB_OP_UNUSED);
            }
        }

        return v->reg;
    }

    // ─── Helper: operand with component offset ──────────────────────────
    // If 'base' is a register (0x00-0x1F), return base+offset.
    // If 'base' is a literal/constant/uniform, return base if offset=0,
    // else broadcast (same value for all components).
    uint8_t OperandOffset(uint8_t base, uint8_t offset, ValType sourceType) {
        // Scalar source → broadcast to all components
        if (sourceType == VTYPE_FLOAT && offset > 0) return base;

        // Register range
        if (base <= PSB_OP_REG_END) return base + offset;

        // Uniform range — consecutive slots
        if (base >= PSB_OP_UNIFORM_BASE && base <= PSB_OP_UNIFORM_END) {
            return base + offset;
        }

        // Constant or literal — no offset (scalar broadcast)
        return base;
    }

    // ─── Math Opcode Lookup ─────────────────────────────────────────────

    uint8_t GetMathOpcode(const char* fname) {
        if (strcmp(fname, "sin") == 0)         return PSB_OP_SIN;
        if (strcmp(fname, "cos") == 0)         return PSB_OP_COS;
        if (strcmp(fname, "tan") == 0)         return PSB_OP_TAN;
        if (strcmp(fname, "asin") == 0)        return PSB_OP_ASIN;
        if (strcmp(fname, "acos") == 0)        return PSB_OP_ACOS;
        if (strcmp(fname, "atan") == 0)        return PSB_OP_ATAN;
        if (strcmp(fname, "pow") == 0)         return PSB_OP_POW;
        if (strcmp(fname, "exp") == 0)         return PSB_OP_EXP;
        if (strcmp(fname, "exp2") == 0)        return PSB_OP_EXP;  // exp2→exp approximation
        if (strcmp(fname, "log") == 0)         return PSB_OP_LOG;
        if (strcmp(fname, "log2") == 0)        return PSB_OP_LOG;  // log2→log approximation
        if (strcmp(fname, "sqrt") == 0)        return PSB_OP_SQRT;
        if (strcmp(fname, "inversesqrt") == 0) return PSB_OP_RSQRT;
        if (strcmp(fname, "abs") == 0)         return PSB_OP_ABS;
        if (strcmp(fname, "sign") == 0)        return PSB_OP_SIGN;
        if (strcmp(fname, "floor") == 0)       return PSB_OP_FLOOR;
        if (strcmp(fname, "ceil") == 0)        return PSB_OP_CEIL;
        if (strcmp(fname, "fract") == 0)       return PSB_OP_FRACT;
        if (strcmp(fname, "mod") == 0)         return PSB_OP_MOD;
        if (strcmp(fname, "min") == 0)         return PSB_OP_MIN;
        if (strcmp(fname, "max") == 0)         return PSB_OP_MAX;
        if (strcmp(fname, "clamp") == 0)       return PSB_OP_CLAMP;
        if (strcmp(fname, "mix") == 0)         return PSB_OP_MIX;
        if (strcmp(fname, "step") == 0)        return PSB_OP_STEP;
        if (strcmp(fname, "smoothstep") == 0)  return PSB_OP_SSTEP;
        return 0; // not a math function
    }

    // ─── Instruction Emission ───────────────────────────────────────────

    void EmitInstruction(uint8_t opcode, uint8_t dst, uint8_t srcA, uint8_t srcB) {
        if (instrCount_ >= MAX_INSTRS) {
            Error(0, "instruction limit exceeded");
            return;
        }
        PglShaderInstruction& instr = instrs_[instrCount_++];
        instr.opcode = opcode;
        instr.dst = dst;
        instr.srcA = srcA;
        instr.srcB = srcB;
    }

    // ─── Constant Pool ──────────────────────────────────────────────────

    uint8_t AddConstant(float value) {
        // Check for duplicate
        for (int i = 0; i < constCount_; ++i) {
            if (constPool_[i] == value) {
                return static_cast<uint8_t>(i);
            }
        }

        if (constCount_ >= PSB_MAX_CONSTANTS) {
            Error(0, "constant pool full");
            return 0;
        }

        uint8_t idx = static_cast<uint8_t>(constCount_);
        constPool_[constCount_++] = value;
        return idx;
    }

    // ─── PSB Binary Assembly ────────────────────────────────────────────

    bool AssembleBinary(CompileResult& result) {
        uint8_t* out = result.bytecode;
        uint16_t pos = 0;

        // Header
        PglShaderProgramHeader hdr{};
        hdr.magic       = PSB_MAGIC;
        hdr.version     = PSB_VERSION;
        hdr.flags       = needsScratchCopy_ ? PSB_FLAG_NEEDS_SCRATCH_COPY : 0;
        hdr.constCount  = static_cast<uint8_t>(constCount_);
        hdr.uniformCount = static_cast<uint8_t>(uniformCount_);
        hdr.instrCount  = static_cast<uint16_t>(instrCount_);
        hdr.nameHash    = 0; // Could hash the source
        hdr.reserved    = 0;

        if (pos + sizeof(hdr) > PSB_MAX_PROGRAM_SIZE) goto overflow;
        std::memcpy(out + pos, &hdr, sizeof(hdr));
        pos += sizeof(hdr);

        // Uniform descriptor table
        for (int i = 0; i < uniformCount_; ++i) {
            PglUniformDescriptor desc{};
            desc.nameHash = uniforms_[i].nameHash;
            desc.type     = static_cast<uint8_t>(uniforms_[i].type) - 1; // FLOAT=0, VEC2=1, ...
            desc.slot     = uniforms_[i].slot;
            desc.defaultValueOffset = 0;

            if (pos + sizeof(desc) > PSB_MAX_PROGRAM_SIZE) goto overflow;
            std::memcpy(out + pos, &desc, sizeof(desc));
            pos += sizeof(desc);
        }

        // Constants pool
        for (int i = 0; i < constCount_; ++i) {
            if (pos + sizeof(float) > PSB_MAX_PROGRAM_SIZE) goto overflow;
            std::memcpy(out + pos, &constPool_[i], sizeof(float));
            pos += sizeof(float);
        }

        // Instructions (pack each as uint32_t: opcode | dst<<8 | srcA<<16 | srcB<<24)
        for (int i = 0; i < instrCount_; ++i) {
            uint32_t packed = static_cast<uint32_t>(instrs_[i].opcode)
                            | (static_cast<uint32_t>(instrs_[i].dst)  << 8)
                            | (static_cast<uint32_t>(instrs_[i].srcA) << 16)
                            | (static_cast<uint32_t>(instrs_[i].srcB) << 24);

            if (pos + sizeof(uint32_t) > PSB_MAX_PROGRAM_SIZE) goto overflow;
            std::memcpy(out + pos, &packed, sizeof(uint32_t));
            pos += sizeof(uint32_t);
        }

        result.bytecodeSize = pos;
        return true;

    overflow:
        Error(0, "program too large");
        return false;
    }
};

