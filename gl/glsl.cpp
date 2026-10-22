// GLSL for the GL renderer, translated from the SDK's HLSL generator (sdk/runtime/gpu/shaders.cpp) step for step:
// the same microcode fields, register combiner rules and fixed function formulas, so a difference between the
// renderers is a GL matter. The GL side's conventions:
//   - render target rows are kept in the console's order (row 0 at the top): the vertex shader negates y, so the
//     host never flips a texture, a read-back or an upload; only the final picture is flipped onto the screen;
//   - depth is the console's 0..1, sent as 2z - w for GL's -1..1 clip range;
//   - matrices are the D3D12 path's (transposed for column-major packing), multiplied as row vector * matrix.
#include "gl/glsl.h"

#include "gpu/vertex.h"

#include <algorithm>
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace glsl {

using namespace gpu;

namespace {

std::string F(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

const char* kHead = "#version 430 core\n";

// both uniform blocks, laid out as s.vsc and gpu::DrawConstants
const char* kUniforms = R"(
layout(std140, binding = 0) uniform VC { vec4 c[192]; };
layout(std140, binding = 1) uniform DC {
    vec4 surf;
    vec4 fogp;
    vec4 fogcolor;
    vec4 misc;
    vec4 pc[18];
    vec4 tfactor;
    vec4 texsize[4];
    vec4 bumpmat[4];
    vec4 bumplum[4];
    mat4 wvp;
    vec4 ext;
    vec4 spread[4];
    mat4 wv;
    mat4 wvn;
    mat4 tm[4];
    vec4 mat[5];
    vec4 amb;
    vec4 lt[56];
    vec4 rtshift;
};
float saturate(float v) { return clamp(v, 0.0, 1.0); }
vec2 saturate(vec2 v) { return clamp(v, 0.0, 1.0); }
vec3 saturate(vec3 v) { return clamp(v, 0.0, 1.0); }
vec4 saturate(vec4 v) { return clamp(v, 0.0, 1.0); }
#define lerp mix
#define INF uintBitsToFloat(0x7F800000u)
)";

// what the vertex shader hands the pixel shader; e: a quad's positions as drawn and as pulled out
std::string Varyings(const char* dir, bool quad) {
    std::string s = F("%s V {\n    vec4 d0;\n    vec4 d1;\n    vec4 b0;\n    vec4 b1;\n    vec4 tc0;\n    vec4 tc1;\n"
                      "    vec4 tc2;\n    vec4 tc3;\n    float fogamt;\n", dir);
    if (quad) s += "    noperspective vec4 e;\n";
    return s + F("} %s;\n", dir[0] == 'o' ? "o" : "i");
}

// vertex programs

const char* kVsHelpers = R"(
vec4 mac_mul(vec4 a, vec4 b) { return mix(a * b, vec4(0.0), bvec4(vec4(equal(a, vec4(0.0))) + vec4(equal(b, vec4(0.0))))); }
int mac_arl(float v) { return int(floor(v + 1e-3)); }
// magnitude clamped between 1 / 2^64 and 2^64, sign kept
float magclamp(float v) { float m = clamp(abs(v), 5.42101e-20, 1.84467e+19); return v < 0.0 ? -m : m; }
vec4 ilu_rcp(float v) { return vec4(1.0 / v); }
vec4 ilu_rcc(float v) { return vec4(magclamp(1.0 / v)); }
vec4 ilu_rsq(float v) { return v == 0.0 ? vec4(INF) : vec4(inversesqrt(abs(v))); }
// 2^floor(v), fraction, 2^v
vec4 ilu_exp(float v) {
    float whole = floor(v);
    return vec4(exp2(whole), v - whole, exp2(v), 1.0);
}
// exponent, mantissa, log2 of |v|; zero logs to negative infinity
vec4 ilu_log(float v) {
    float mag = abs(v), l2 = log2(mag), ex = floor(l2);
    return mag == 0.0 ? vec4(-INF, 1.0, -INF, 1.0) : vec4(ex, mag / exp2(ex), l2, 1.0);
}
// power clamped to 128 less 1/256
vec4 ilu_lit(vec4 s) {
    const float cap = 127.99609375;
    float diffuse = max(s.x, 0.0);
    float spec = exp2(clamp(s.w, -cap, cap) * log2(max(s.y, 0.0)));
    return vec4(1.0, diffuse, diffuse > 0.0 ? spec : 0.0, 1.0);
}
float fogfactor(float d) {
    if (fogp.w == 3.0) return (fogp.y - d) / (fogp.y - fogp.x);
    if (fogp.w == 1.0) return exp(-d * fogp.z);
    if (fogp.w == 2.0) return exp(-(d * fogp.z) * (d * fogp.z));
    return 1.0;
}
)";

struct Tok {
    const uint32_t* t;
    uint32_t f(int sub, int bit, int len) const { return (t[sub] >> bit) & ((1u << len) - 1); }
};

const char* kMask[16] = {"", "w", "z", "zw", "y", "yw", "yz", "yzw", "x", "xw", "xz", "xzw", "xy", "xyw", "xyz", "xyzw"};
const char* kLanes[5] = {"", "x", "xy", "xyz", "xyzw"};  // oFog takes one lane per mask bit, from x
const char* kOut[16] = {"oPos", "", "", "oD0", "oD1", "oFog", "oPts", "oB0", "oB1", "oT0", "oT1", "oT2", "oT3", "", "", ""};

std::string Input(uint16_t regs, uint32_t v) {
    if ((regs >> v) & 1) return F("v%u", v);
    return v == 3 ? "vec4(1.0)" : "vec4(0.0, 0.0, 0.0, 1.0)";
}

// operand A 0, B 1, C 2
std::string Src(const Tok& k, int which, uint16_t regs) {
    uint32_t neg, sw[4], reg, mux;
    if (which == 0) {
        neg = k.f(1, 8, 1);
        sw[0] = k.f(1, 6, 2), sw[1] = k.f(1, 4, 2), sw[2] = k.f(1, 2, 2), sw[3] = k.f(1, 0, 2);
        reg = k.f(2, 28, 4), mux = k.f(2, 26, 2);
    } else if (which == 1) {
        neg = k.f(2, 25, 1);
        sw[0] = k.f(2, 23, 2), sw[1] = k.f(2, 21, 2), sw[2] = k.f(2, 19, 2), sw[3] = k.f(2, 17, 2);
        reg = k.f(2, 13, 4), mux = k.f(2, 11, 2);
    } else {
        neg = k.f(2, 10, 1);
        sw[0] = k.f(2, 8, 2), sw[1] = k.f(2, 6, 2), sw[2] = k.f(2, 4, 2), sw[3] = k.f(2, 2, 2);
        reg = k.f(2, 0, 2) << 2 | k.f(3, 30, 2), mux = k.f(3, 28, 2);
        uint32_t ilu = k.f(1, 25, 3);
        if (ilu >= 2 && ilu <= 6) sw[1] = sw[2] = sw[3] = sw[0];  // scalar ops read x
    }
    // mux: 1 temporary (R12 reads oPos), 2 input, 3 constant
    std::string base;
    if (mux == 1) {
        base = reg == 12 ? "oPos" : F("R%u", reg > 12 ? 0 : reg);
    } else if (mux == 2) {
        base = Input(regs, k.f(1, 9, 4));
    } else {
        uint32_t cr = k.f(1, 13, 8);
        base = k.f(3, 1, 1) ? F("c[clamp(a0 + %u, 0, 191)]", cr) : F("c[%u]", std::min<uint32_t>(cr, 191));
    }
    const char* xyzw = "xyzw";
    return F("(%s(%s).%c%c%c%c)", neg ? "-" : "", base.c_str(), xyzw[sw[0]], xyzw[sw[1]], xyzw[sw[2]], xyzw[sw[3]]);
}

// MAC opcodes from 1: mov mul add mad dp3 dph dp4 dst min max slt sge arl
std::string MacExpr(uint32_t op, const std::string& a, const std::string& b, const std::string& c) {
    switch (op) {
        case 1: return a;
        case 2: return "mac_mul(" + a + ", " + b + ")";
        case 3: return "(" + a + " + " + c + ")";
        case 4: return "(mac_mul(" + a + ", " + b + ") + " + c + ")";
        case 5: return "vec4(dot(" + a + ".xyz, " + b + ".xyz))";
        case 6: return "vec4(dot(vec4(" + a + ".xyz, 1.0), " + b + "))";
        case 7: return "vec4(dot(" + a + ", " + b + "))";
        case 8: return "vec4(1.0, " + a + ".y * " + b + ".y, " + a + ".z, " + b + ".w)";
        case 9: return "min(" + a + ", " + b + ")";
        case 10: return "max(" + a + ", " + b + ")";
        case 11: return "vec4(lessThan(" + a + ", " + b + "))";
        case 12: return "vec4(greaterThanEqual(" + a + ", " + b + "))";
        case 13: return a;
        default: return "vec4(0.0)";
    }
}

// ILU opcodes from 1: mov rcp rcc rsq exp log lit
std::string IluExpr(uint32_t op, const std::string& c) {
    switch (op) {
        case 1: return c;
        case 2: return "ilu_rcp(" + c + ".x)";
        case 3: return "ilu_rcc(" + c + ".x)";
        case 4: return "ilu_rsq(" + c + ".x)";
        case 5: return "ilu_exp(" + c + ".x)";
        case 6: return "ilu_log(" + c + ".x)";
        case 7: return "ilu_lit(" + c + ")";
        default: return "vec4(0.0)";
    }
}

std::string RegDest(uint32_t r) { return r == 12 ? "oPos" : F("R%u", r); }

// the input assembler converts every element to a vec4 but two: NORMPACKED3 comes as its uint, FLOAT2H as x y w
std::string VsInputs(uint16_t regs, uint64_t decode) {
    std::string s;
    bool packed = false;
    for (int r = 0; r < 16; r++) {
        if (!((regs >> r) & 1)) continue;
        uint32_t d = uint32_t(decode >> (r * 4)) & 15;
        if (d == gpu::kDecodePacked) {
            s += F("layout(location = %d) in uint p%d;\nvec4 v%d;\n", r, r, r);
            packed = true;
        } else if (d == gpu::kDecodeFloat2H) {
            s += F("layout(location = %d) in vec4 p%d;\nvec4 v%d;\n", r, r, r);
        } else {
            s += F("layout(location = %d) in vec4 v%d;\n", r, r);
        }
    }
    if (packed)
        s += "vec4 unpack_np3(uint u) {\n"
             "    ivec3 v = ivec3(int(u << 21) >> 21, int(u << 10) >> 21, int(u) >> 22);\n"
             "    return vec4(max(vec3(v) / vec3(1023.0, 1023.0, 511.0), vec3(-1.0)), 1.0);\n"
             "}\n";
    return s;
}

std::string VsHead(uint16_t regs, uint64_t decode, bool quad) {
    std::string s = std::string(kHead) + kUniforms + kVsHelpers + VsInputs(regs, decode) + Varyings("out", quad) + "void main() {\n";
    for (int r = 0; r < 16; r++) {
        if (!((regs >> r) & 1)) continue;
        uint32_t d = uint32_t(decode >> (r * 4)) & 15;
        if (d == gpu::kDecodePacked) s += F("    v%d = unpack_np3(p%d);\n", r, r);
        if (d == gpu::kDecodeFloat2H) s += F("    v%d = vec4(p%d.xy, 0.0, p%d.z);\n", r, r, r);
    }
    return s;
}

const char* kVsPrologue = R"(
    int a0 = 0;
    // precise: EQUAL depth passes need bit exact positions
    precise vec4 R0 = vec4(0.0), R1 = vec4(0.0), R2 = vec4(0.0), R3 = vec4(0.0), R4 = vec4(0.0), R5 = vec4(0.0), R6 = vec4(0.0),
        R7 = vec4(0.0), R8 = vec4(0.0), R9 = vec4(0.0), R10 = vec4(0.0), R11 = vec4(0.0);
    precise vec4 oPos = vec4(0.0);
    vec4 oD0 = vec4(0.0, 0.0, 0.0, 1.0), oD1 = vec4(0.0), oB0 = vec4(0.0, 0.0, 0.0, 1.0), oB1 = vec4(0.0), oFog = vec4(0.0), oPts = vec4(1.0);
    vec4 oT0 = vec4(0.0, 0.0, 0.0, 1.0), oT1 = vec4(0.0, 0.0, 0.0, 1.0), oT2 = vec4(0.0, 0.0, 0.0, 1.0), oT3 = vec4(0.0, 0.0, 0.0, 1.0);
)";

// screen space oPos to GL: the console's rows and depth, see the top of the file
std::string VsEpilogue(bool quad) {
    std::string s = R"(
    precise vec4 p = oPos;
    p.w = magclamp(p.w);
    precise vec4 q;
)";
    if (quad) s += "    o.e.xy = p.xy;\n";
    s += R"(    // full screen passes drawn half a pixel up-left for the console's integer pixel centers stop half a pixel short
    // of the far edges on the host; surf.w set: a point, which stays put since pulled out it would leave the target
    if (surf.w == 0.0 && abs(p.x - (surf.x - 0.5)) < 1e-3) p.x = surf.x;
    if (surf.w == 0.0 && abs(p.y - (surf.y - 0.5)) < 1e-3) p.y = surf.y;
)";
    if (quad) s += "    o.e.zw = p.xy;\n";
    s += R"(    q.x = p.x * 2.0 / surf.x - 1.0;
    q.y = 1.0 - p.y * 2.0 / surf.y;
    q.z = p.z * surf.z;
    q.xyz *= p.w;
    q.w = p.w;
    gl_Position = vec4(q.x, -q.y, 2.0 * q.z - q.w, q.w);
    o.d0 = saturate(oD0);
    o.d1 = saturate(oD1);
    o.b0 = saturate(oB0);
    o.b1 = saturate(oB1);
    o.tc0 = oT0;
    o.tc1 = oT1;
    o.tc2 = oT2;
    o.tc3 = oT3;
    o.fogamt = fogfactor(oFog.x);
}
)";
    return s;
}

// combiners

struct In {
    uint32_t reg, chan, mod;
};
In ParseIn(uint32_t v) { return {v & 0xF, v & 0x10, v & 0xE0}; }

struct Ps {
    const PixelKey& k;
    int stage = 0;
    std::string e, f;  // final combiner E F for EF_PROD
    bool clamp_sum = false, inv_v1 = false, inv_r0 = false;
    std::string code;

    std::string Var(uint32_t reg) {
        uint32_t flags = k.count >> 8;  // PS_COMBINERCOUNT flags: 0x10 unique C0 per stage, 0x100 unique C1
        switch (reg) {
            case 0: return "vec4(0.0)";
            case 1: return F("pc[%d]", stage == 8 ? 16 : ((flags & 0x10) ? stage * 2 : 0));
            case 2: return F("pc[%d]", stage == 8 ? 17 : ((flags & 0x100) ? stage * 2 + 1 : 1));
            case 3: return "fogin";
            case 4: return "v0";
            case 5: return "v1";
            case 8: return "t0";
            case 9: return "t1";
            case 10: return "t2";
            case 11: return "t3";
            case 12: return "r0";
            case 13: return "r1";
            case 14:
                return F("%s(vec4(%s.rgb + %s.rgb, 0.0))", clamp_sum ? "saturate" : "", inv_v1 ? "(1.0 - v1)" : "v1",
                         inv_r0 ? "(1.0 - r0)" : "r0");
            case 15: return "vec4(" + e + " * " + f + ", 0.0)";
            default: return "vec4(0.0)";
        }
    }

    std::string Input(In in, bool alpha) {
        std::string r = Var(in.reg);
        if (!alpha) r += in.chan ? ".aaa" : ".rgb";
        else r += in.chan ? ".a" : ".b";
        switch (in.mod) {
            // PS_INPUTMAPPING
            case 0x00: return "max(" + r + ", 0.0)";
            case 0x20: return "(1.0 - saturate(" + r + "))";
            case 0x40: return "(2.0 * max(" + r + ", 0.0) - 1.0)";
            case 0x60: return "(-2.0 * max(" + r + ", 0.0) + 1.0)";
            case 0x80: return "(max(" + r + ", 0.0) - 0.5)";
            case 0xA0: return "(-max(" + r + ", 0.0) + 0.5)";
            case 0xC0: return r;
            default: return "(-" + r + ")";
        }
    }

    static std::string Map(const std::string& v, uint32_t m) {
        switch (m) {
            // PS_COMBINEROUTPUT bias and shift
            case 0x08: return "(" + v + " - 0.5)";
            case 0x10: return "(" + v + " * 2.0)";
            case 0x18: return "((" + v + " - 0.5) * 2.0)";
            case 0x20: return "(" + v + " * 4.0)";
            case 0x30: return "(" + v + " / 2.0)";
            default: return v;
        }
    }

    static const char* Dest(uint32_t reg) {
        switch (reg) {
            case 4: return "v0";
            case 5: return "v1";
            case 8: return "t0";
            case 9: return "t1";
            case 10: return "t2";
            case 11: return "t3";
            case 12: return "r0";
            case 13: return "r1";
            default: return nullptr;
        }
    }

    // rgb and alpha halves read the registers before either writes, so the writes come back to the caller
    std::string Stage(uint32_t inputs, uint32_t output, bool alpha) {
        In d = ParseIn(inputs & 0xFF), c = ParseIn((inputs >> 8) & 0xFF), b = ParseIn((inputs >> 16) & 0xFF),
           a = ParseIn(inputs >> 24);
        // fl: 1 cd dot, 2 ab dot, 4 mux, 0x38 mapping, 0x40 and 0x80 blue to alpha
        uint32_t cd_dst = output & 0xF, ab_dst = (output >> 4) & 0xF, sum_dst = (output >> 8) & 0xF, fl = output >> 12;
        const char* m = alpha ? "a" : "rgb";
        const char* T = alpha ? "float" : "vec3";
        const char* n = alpha ? "a" : "c";
        std::string A = Input(a, alpha), B = Input(b, alpha), C = Input(c, alpha), D = Input(d, alpha);
        std::string ab = (fl & 2) ? "dot(" + A + ", " + B + ")" : "(" + A + " * " + B + ")";
        std::string cd = (fl & 1) ? "dot(" + C + ", " + D + ")" : "(" + C + " * " + D + ")";
        uint32_t map = fl & 0x38;
        code += F("%s ab%s = clamp(%s(%s), -1.0, 1.0);\n", T, n, T, Map(ab, map).c_str());
        code += F("%s cd%s = clamp(%s(%s), -1.0, 1.0);\n", T, n, T, Map(cd, map).c_str());
        std::string sum = (fl & 4) ? F("((%s) ? %s(%s) : %s(%s))",
                                       ((k.count >> 8) & 1) ? "r0.a >= 0.5" : "(uint(r0.a * 255.0) & 1u) == 1u", T,
                                       cd.c_str(), T, ab.c_str())
                                   : "(" + ab + " + " + cd + ")";
        code += F("%s ms%s = clamp(%s(%s), -1.0, 1.0);\n", T, n, T, Map(sum, map).c_str());
        std::string post;
        if (const char* r = Dest(ab_dst)) {
            post += F("%s.%s = ab%s;\n", r, m, n);
            if (!alpha && (fl & 0x80)) post += F("%s.a = abc.b;\n", r);
        }
        if (const char* r = Dest(cd_dst)) {
            post += F("%s.%s = cd%s;\n", r, m, n);
            if (!alpha && (fl & 0x40)) post += F("%s.a = cdc.b;\n", r);
        }
        if (const char* r = Dest(sum_dst)) post += F("%s.%s = ms%s;\n", r, m, n);
        return post;
    }
};

// a dot stage's operand: the source stage's rgb, raw or read back as signed bytes
std::string DotInput(uint32_t map, uint32_t src) {
    switch (map) {
        case 1: return F("sbyte(t%u.rgb, 128.0, 128.0, 127.0)", src);        // offset by 128
        case 2: return F("sbyte(t%u.rgb, -0.5, 255.5, 127.5)", src);       // two's complement, (2b + 1) / 255
        case 3: return F("sbyte(t%u.rgb, 0.0, 256.0, 127.0)", src);          // two's complement
        default: return F("t%u.rgb", src);
    }
}

// stage i's coordinates as the title wrote them, over 0..1. The console draws half a pixel right and down of the
// host unless a program writes positions raw: a render target is read where the console's offset puts it
std::string TexUv(const TexInfo& t, int i, const std::string& uv) {
    std::string n = "(" + uv + ")";
    if (t.rect) n += F(" / texsize[%d].xy", i);
    n = "(" + n + F(" + texsize[%d].zw)", i);
    if (!(t.flags & kTexTarget)) return n;
    return "(" + n + F(" + rtshift[%d] * (dFdx(%s) + dFdy(%s)))", i, n.c_str(), n.c_str());
}

std::string Sample(int i, const std::string& coord) { return F("texture(tex%d, %s)", i, coord.c_str()); }

// texture shader stage i, per its mode
std::string TexFetch(const PixelKey& k, int i, bool quad) {
    uint32_t mode = (k.tex_modes >> (i * 5)) & 0x1F;
    const TexInfo& t = k.tex[i];
    uint32_t in = i == 1 ? 0 : i == 2 ? (k.input_texture >> 16) & 0xF : (k.input_texture >> 20) & 0xF;
    uint32_t dm = i == 0 ? 0 : (k.dot_mapping >> ((i - 1) * 4)) & 0xF;
    const std::string tc = F("tc%d", i);
    const char* q = tc.c_str();
    (void)quad;
    auto put = [&](const std::string& v) { return F("vec4 t%d = %s;\n", i, v.c_str()); };
    // dpN, or dpNn for stage N + 1's product taken early
    auto dp = [&](int n, int coord, uint32_t map, uint32_t src) {
        return F("float dp%d%s = dot(tc%d.xyz, %s);\n", n, coord == n ? "" : "n", coord, DotInput(map, src).c_str());
    };
    auto sample2 = [&](const std::string& uv) {
        if (t.kind == kTex2D) return Sample(i, TexUv(t, i, uv));
        if (t.kind == kTexCube) return Sample(i, "vec3(" + uv + ", 1.0)");
        if (t.kind == kTex3D) return Sample(i, "vec3(" + uv + ", 0.0)");
        return std::string("vec4(0.0, 0.0, 0.0, 1.0)");
    };
    auto sample3 = [&](const std::string& uvw) {
        if (t.kind == kTex2D) return Sample(i, TexUv(t, i, "(" + uvw + ").xy"));
        if (t.kind == kTexCube || t.kind == kTex3D) return Sample(i, uvw);
        return std::string("vec4(0.0, 0.0, 0.0, 1.0)");
    };
    auto depth = [&](const std::string& ref) {
        std::string uv = TexUv(t, i, F("%s.xy / %s.w", q, q));
        return put(F("shadow(tex%d, %s, %s, %d)", i, uv.c_str(), ref.c_str(), t.shadow));
    };
    std::string s;
    switch (mode) {
        // PS_TEXTUREMODES: 1 project2d, 2 project3d, 3 cubemap, 4 passthru, 5 clipplane, 6 and 7 bumpenvmap, 9 dot_st,
        // 0xA dot_zw, 0xD dot_str_3d, 0xE dot_str_cube, 0xF dpndnt_ar, 0x10 dpndnt_gb, 0x11 dotproduct
        case 0: return put("vec4(0.0, 0.0, 0.0, 1.0)");
        case 1: return t.shadow ? depth("0.0") : put(sample2(F("%s.xy / %s.w", q, q)));
        case 2: return t.shadow ? depth(F("%s.z / %s.w", q, q)) : put(sample3(F("%s.xyz / %s.w", q, q)));
        case 3: return put(sample3(tc + ".xyz"));
        case 4: return put(tc);
        case 5:  // clip plane: a set compare_mode bit kills at >= 0, a clear one below 0
            s = put("vec4(0.0)");
            for (int j = 0; j < 4; j++)
                s += F("if (%s.%c %s 0.0) discard;\n", q, "xyzw"[j], ((k.compare_mode >> (i * 4 + j)) & 1) ? ">=" : "<");
            return s;
        case 6:
        case 7:  // bump env map: signed du dv through the 2x2 matrix, 7 scales by luminance
            s = F("vec2 duv%d = sbyte(t%u.bgr, 0.0, 256.0, 127.0).xy;\n", i, in);
            s += put(sample2(F("%s.xy + vec2(dot(bumpmat[%d].xz, duv%d), dot(bumpmat[%d].yw, duv%d))", q, i, i, i, i)));
            if (mode == 7) s += F("t%d *= bumplum[%d].x * t%u.r + bumplum[%d].y;\n", i, i, in, i);
            return s;
        case 9: return dp(i, i, dm, in) + put(sample2(F("vec2(dp%d, dp%d)", i - 1, i)));
        case 0xA:
        case 0x11: return dp(i, i, dm, in) + put("vec4(0.0)");
        case 0xB:  // reflect diffuse at stage 2: stage 3's product early for the normal
            s = dp(i, i, dm, in) + dp(i, i + 1, (k.dot_mapping >> 8) & 0xF, (k.input_texture >> 20) & 0xF);
            return s + put(sample3(F("vec3(dp%d, dp%d, dp%dn)", i - 1, i, i)));
        case 0xC:  // reflect specular at stage 3: eye from the three w
            s = dp(i, i, dm, in);
            s += F("vec3 nrm%d = vec3(dp%d, dp%d, dp%d), eye%d = vec3(tc%d.w, tc%d.w, tc%d.w);\n", i, i - 2, i - 1, i, i, i - 2,
                   i - 1, i);
            s += F("vec3 refl%d = 2.0 * nrm%d * dot(nrm%d, eye%d) / dot(nrm%d, nrm%d) - eye%d;\n", i, i, i, i, i, i, i);
            return s + put(sample3(F("refl%d", i)));
        case 0xD:
        case 0xE: return dp(i, i, dm, in) + put(sample3(F("vec3(dp%d, dp%d, dp%d)", i - 2, i - 1, i)));
        case 0xF: return put(sample2(F("t%u.ar", in)));
        case 0x10: return put(sample2(F("t%u.gb", in)));
        default: return put("vec4(0.0)");
    }
}

const char* kPsHelpers = R"(
// unorm to byte b, less lo under 128 or hi from 128, over d
vec3 sbyte(vec3 u, float lo, float hi, float d) { vec3 b = u * 255.0; return (b - mix(vec3(lo), vec3(hi), greaterThanEqual(b, vec3(128.0)))) / d; }
// the sampler compares; a linear filter blends the four results like the console does
vec4 shadow(sampler2DShadow t, vec2 uv, float z, int fmt) {
    float mx = fmt == 2 ? 65535.0 : 16777215.0;
    return vec4(textureLod(t, vec3(uv, clamp(z, 0.0, mx) / mx), 0.0));
}
void alphatest(float a) {
    int f = int(misc.y);
    int v = int(round(a * 255.0)), r = int(misc.x);
    bool ok = f == 0 ? false : f == 1 ? v < r : f == 2 ? v == r : f == 3 ? v <= r : f == 4 ? v > r : f == 5 ? v != r : f == 6 ? v >= r : true;
    if (!ok) discard;
}
)";

// a quad's coordinates as drawn: the pull out of its far edges taken back out
const char* kUnstretch = R"(
vec4 unstretch(vec4 t, vec4 e) {
    vec2 d = e.zw - e.xy, g = vec2(dFdx(e.x), dFdy(e.y));
    return t + (d.x != 0.0 ? d.x / g.x * dFdx(t) : vec4(0.0)) + (d.y != 0.0 ? d.y / g.y * dFdy(t) : vec4(0.0));
}
)";

// fixed function texture stage arguments, D3DTA
std::string FfArg(uint32_t a, int stage, bool alpha) {
    std::string v;
    switch (a & 0xF) {
        case 0: v = "v0"; break;
        case 1: v = "cur"; break;
        case 2: v = F("t%d", stage); break;
        case 3: v = "tfactor"; break;
        case 4: v = "v1"; break;
        case 5: v = "tmp"; break;
        default: v = "cur"; break;
    }
    if (a & 0x20) v = "(" + v + ").aaaa";
    if (a & 0x10) v = "(1.0 - " + v + ")";
    return "(" + v + ")" + (alpha ? ".a" : ".rgb");
}

// D3DTOP
std::string FfOp(uint32_t op, const std::string& a1, const std::string& a2, const std::string& a0, int stage, bool alpha) {
    switch (op) {
        case 2: return a1;
        case 3: return a2;
        case 4: return a1 + " * " + a2;
        case 5: return a1 + " * " + a2 + " * 2.0";
        case 6: return a1 + " * " + a2 + " * 4.0";
        case 7: return a1 + " + " + a2;
        case 8: return a1 + " + " + a2 + " - 0.5";
        case 9: return "(" + a1 + " + " + a2 + " - 0.5) * 2.0";
        case 10: return a1 + " - " + a2;
        case 11: return a1 + " + " + a2 + " * (1.0 - " + a1 + ")";
        case 12: return "mix(" + a2 + ", " + a1 + ", v0.a)";
        case 13: return "mix(" + a2 + ", " + a1 + ", cur.a)";
        case 14: return F("mix(%s, %s, t%d.a)", a2.c_str(), a1.c_str(), stage);
        case 15: return "mix(" + a2 + ", " + a1 + ", tfactor.a)";
        case 16: return F("%s + %s * (1.0 - t%d.a)", a1.c_str(), a2.c_str(), stage);
        case 18: return alpha ? a1 : a1 + " + " + a2 + " * " + a1.substr(0, a1.size() - 4) + ".a";
        case 19: return alpha ? a1 : a1 + " * " + a2 + " + " + a1.substr(0, a1.size() - 4) + ".a";
        case 20: return alpha ? a1 : a1 + " + " + a2 + " * (1.0 - " + a1.substr(0, a1.size() - 4) + ".a)";
        case 21: return alpha ? a1 : "(1.0 - " + a1 + ") * " + a2 + " + " + a1.substr(0, a1.size() - 4) + ".a";
        case 22: return F("saturate(dot((%s - 0.5) * 2.0, (%s - 0.5) * 2.0))", alpha ? ("vec3(" + a1 + ", 0.0, 0.0)").c_str() : a1.c_str(),
                          alpha ? ("vec3(" + a2 + ", 0.0, 0.0)").c_str() : a2.c_str());
        case 23: return a0 + " + " + a1 + " * " + a2;
        case 24: return "mix(" + a2 + ", " + a1 + ", " + a0 + ")";
        default: return alpha ? "cur.a" : "cur.rgb";
    }
}

const char* SamplerType(const TexInfo& t) {
    if (t.shadow) return "sampler2DShadow";
    if (t.kind == kTex3D) return "sampler3D";
    if (t.kind == kTexCube) return "samplerCube";
    return "sampler2D";
}

}  // namespace

const char* kFullScreenVertex = R"(#version 430 core
out vec2 uv;
void main() {
    vec2 t = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = t;
    gl_Position = vec4(t * 2.0 - 1.0, 0.0, 1.0);
}
)";

std::string VertexProgram(const std::vector<uint32_t>& prog, uint16_t regs, uint64_t decode, bool quad) {
    std::string s = VsHead(regs, decode, quad) + kVsPrologue;
    for (size_t n = 0; n + 4 <= prog.size(); n += 4) {
        Tok k{&prog[n]};
        uint32_t mac = k.f(1, 21, 4), ilu = k.f(1, 25, 3);
        if (mac || ilu) {
            std::string a = Src(k, 0, regs), b = Src(k, 1, regs), c = Src(k, 2, regs);
            s += "    {\n";
            if (mac) s += "        precise vec4 mr = " + MacExpr(mac, a, b, c) + ";\n";
            if (ilu) s += "        precise vec4 ir = " + IluExpr(ilu, c) + ";\n";
            uint32_t r = k.f(3, 20, 4), omask = k.f(3, 12, 4);
            if (mac == 13) {
                s += "        a0 = mac_arl(mr.x);\n";
            } else if (mac) {
                uint32_t m = k.f(3, 24, 4);
                if (ilu && r == 1) m = 0;
                if (m && r <= 12) s += F("        %s.%s = mr.%s;\n", RegDest(r).c_str(), kMask[m], kMask[m]);
            }
            if (ilu) {
                uint32_t m = k.f(3, 16, 4), ir = mac ? 1 : r;
                if (m && ir <= 12) s += F("        %s.%s = ir.%s;\n", RegDest(ir).c_str(), kMask[m], kMask[m]);
            }
            bool use_ilu = k.f(3, 2, 1) == 1;
            if (omask && k.f(3, 11, 1) && ((use_ilu && ilu) || (!use_ilu && mac && mac != 13))) {
                uint32_t o = k.f(3, 3, 8) & 0xF;
                const char* src = use_ilu ? "ir" : "mr";
                if (kOut[o][0]) {
                    const char* msk = o == 5 ? kLanes[std::popcount(omask)] : kMask[omask];
                    s += F("        %s.%s = %s.%s;\n", kOut[o], msk, src, msk);
                }
            }
            s += "    }\n";
        }
        if (k.f(3, 0, 1)) break;  // last instruction
    }
    return s + VsEpilogue(quad);
}

std::string FixedVertex(uint16_t regs, uint64_t decode, bool pretransformed, const FixedKey& f, bool quad) {
    std::string s = VsHead(regs, decode, quad) + kVsPrologue;
    auto v = [&](int r, const char* def) { return (regs >> r) & 1 ? F("v%d", r) : std::string(def); };
    bool lit = !pretransformed && (f.light & 1);
    if (pretransformed) {
        s += "    vec4 rhw = " + v(0, "vec4(0.0, 0.0, 0.0, 1.0)") + ";\n";
        s += "    float w = rhw.w == 0.0 ? 1.0 : 1.0 / rhw.w;\n";
        s += "    oPos = vec4(rhw.xy, rhw.z / surf.z, w);\n";
    } else {
        s += "    vec4 ip = vec4(" + v(0, "vec4(0.0, 0.0, 0.0, 1.0)") + ".xyz, 1.0);\n";
        s += "    precise vec4 cp = ip * wvp;\n";
        s += "    vec3 vp = (ip * wv).xyz;\n";
        s += "    vec3 vn = (vec4(" + v(2, "vec4(0.0)") + ".xyz, 0.0) * wvn).xyz;\n";
        if (f.light & 8) s += "    vn = dot(vn, vn) > 0.0 ? normalize(vn) : vn;\n";
    }
    if (lit) {
        // D3DMCS: 1 the vertex diffuse, 2 its specular, else the material
        auto src = [&](int field, const char* mat) {
            uint32_t m = (f.light >> (8 + field * 2)) & 3;
            if (m == 1 && ((regs >> 3) & 1)) return std::string("v3");
            if (m == 2 && ((regs >> 4) & 1)) return std::string("v4");
            return std::string(mat);
        };
        s += "    vec4 md = " + src(0, "mat[0]") + ", ms = " + src(1, "mat[2]") + ", ma = " + src(2, "mat[1]") + ", me = " +
             src(3, "mat[3]") + ";\n";
        s += "    vec3 la = vec3(0.0), ld = vec3(0.0), ls = vec3(0.0);\n";
        s += (f.light & 4) ? "    vec3 eye = -normalize(vp);\n" : "    vec3 eye = vec3(0.0, 0.0, 1.0);\n";
        for (int n = 0; n < 8; n++) {
            uint32_t type = (f.types >> (n * 2)) & 3;  // D3DLIGHTTYPE: 1 point, 2 spot, 3 directional
            if (!type) continue;
            int b = n * 7;
            s += "    {\n        vec3 l;\n        float att = 1.0;\n";
            if (type == 3) {
                s += F("        l = -normalize(lt[%d].xyz);\n", b + 4);
            } else {
                s += F("        vec3 d = lt[%d].xyz - vp;\n        float dist = length(d);\n        l = d / max(dist, 1e-6);\n", b + 3);
                s += F("        att = dist > lt[%d].w ? 0.0 : 1.0 / max(lt[%d].x + lt[%d].y * dist + lt[%d].z * dist * dist, 1e-6);\n",
                       b + 3, b + 5, b + 5, b + 5);
                if (type == 2) {
                    s += F("        float rho = dot(-l, normalize(lt[%d].xyz));\n", b + 4);
                    s += F("        att *= rho > lt[%d].x ? 1.0 : rho <= lt[%d].y ? 0.0 : pow(saturate((rho - lt[%d].y) / (lt[%d].x - lt[%d].y)), lt[%d].w);\n",
                           b + 6, b + 6, b + 6, b + 6, b + 6, b + 4);
                }
            }
            s += F("        la += att * lt[%d].rgb;\n", b + 2);
            s += "        float nl = dot(vn, l);\n";
            s += F("        ld += att * lt[%d].rgb * max(nl, 0.0);\n", b);
            if (f.light & 2)
                s += F("        if (nl > 0.0) ls += att * lt[%d].rgb * pow(max(dot(vn, normalize(l + eye)), 0.0), mat[4].x);\n", b + 1);
            s += "    }\n";
        }
        s += "    oD0 = vec4(me.rgb + ma.rgb * (amb.rgb + la) + md.rgb * ld, md.a);\n";
        s += (f.light & 2) ? "    oD1 = vec4(ms.rgb * ls, ms.a);\n" : "    oD1 = vec4(0.0);\n";
    } else {
        s += "    oD0 = " + v(3, "vec4(1.0)") + ";\n";
        s += "    oD1 = " + v(4, "vec4(0.0)") + ";\n";
    }
    s += "    oB0 = oD0;\n    oB1 = oD1;\n";
    for (int t = 0; t < 4; t++) {
        uint32_t u = f.tex[t], set = u & 3, gen = (u >> 4) & 7, count = (u >> 8) & 7, size = (u >> 12) & 7;
        std::string in = v(9 + int(set), "vec4(0.0, 0.0, 0.0, 1.0)");
        if (pretransformed || (!gen && !count)) {
            s += F("    oT%d = %s;\n", t, in.c_str());
            continue;
        }
        // generated coordinates are view space; a transform takes the set with 1 after its last element
        std::string c = gen == 1   ? "vec4(vn, 1.0)"
                        : gen == 2 ? "vec4(vp, 1.0)"
                        : gen == 3 ? "vec4(reflect(normalize(vp), vn), 1.0)"
                        : size == 1 ? F("vec4(%s.x, 1.0, 0.0, 0.0)", in.c_str())
                        : size == 2 ? F("vec4(%s.xy, 1.0, 0.0)", in.c_str())
                        : size == 3 ? F("vec4(%s.xyz, 1.0)", in.c_str())
                                    : in;
        if (!count) {
            s += F("    oT%d = %s;\n", t, c.c_str());
            continue;
        }
        s += F("    {\n        vec4 tc = %s * tm[%d];\n", c.c_str(), t);
        if (u & 0x800) s += F("        oT%d = vec4(tc.xy, tc.z, tc[%u]);\n", t, std::min<uint32_t>(count, 4) - 1);
        else s += F("        oT%d = vec4(tc.xy, %s, 1.0);\n", t, count >= 3 ? "tc.z" : "0.0");
        s += "    }\n";
    }
    s += "    oFog = " + v(5, "vec4(0.0)") + ";\n";
    if (pretransformed) return s + VsEpilogue(quad);
    s += R"(
    // like the pretransformed ones, target sized passes end half a console pixel short of the far edges
    vec2 sp = (vec2(cp.x, -cp.y) / cp.w + 1.0) * 0.5 * surf.xy;
    if (surf.w == 0.0 && abs(sp.x - (surf.x - 0.5)) < 1e-3) cp.x = cp.w;
    if (surf.w == 0.0 && abs(sp.y - (surf.y - 0.5)) < 1e-3) cp.y = -cp.w;
)";
    if (quad) s += "    o.e = vec4(sp, (vec2(cp.x, -cp.y) / cp.w + 1.0) * 0.5 * surf.xy);\n";
    s += R"(    gl_Position = vec4(cp.x, -cp.y, 2.0 * cp.z - cp.w, cp.w);
    o.d0 = saturate(oD0);
    o.d1 = saturate(oD1);
    o.b0 = o.d0;
    o.b1 = o.d1;
    o.tc0 = oT0;
    o.tc1 = oT1;
    o.tc2 = oT2;
    o.tc3 = oT3;
    o.fogamt = fogfactor(abs(cp.z));
}
)";
    return s;
}

std::string Pixel(const PixelKey& k) {
    bool quad = k.quad != 0;
    std::string s = std::string(kHead) + kUniforms + Varyings("in", quad) + "layout(location = 0) out vec4 outColor;\n";
    for (int i = 0; i < 4; i++) s += F("layout(binding = %d) uniform %s tex%d;\n", i, SamplerType(k.tex[i]), i);
    s += kPsHelpers;
    if (quad) s += kUnstretch;
    s += "void main() {\n";
    if (quad) s += "    vec4 tc0 = unstretch(i.tc0, i.e), tc1 = unstretch(i.tc1, i.e), tc2 = unstretch(i.tc2, i.e), tc3 = unstretch(i.tc3, i.e);\n";
    else s += "    vec4 tc0 = i.tc0, tc1 = i.tc1, tc2 = i.tc2, tc3 = i.tc3;\n";
    s += k.two_sided ? "    vec4 v0 = gl_FrontFacing ? i.d0 : i.b0, v1 = gl_FrontFacing ? i.d1 : i.b1;\n" : "    vec4 v0 = i.d0, v1 = i.d1;\n";
    s += "    if (misc.w == 0.0) v1 = vec4(0.0, 0.0, 0.0, 1.0);\n";
    s += "    vec4 fogin = vec4(fogcolor.rgb, saturate(i.fogamt));\n";
    std::string body;
    if (k.programmable) {
        for (int i = 0; i < 4; i++) body += TexFetch(k, i, quad);
        for (int i = 0; i < 4; i++) {
            uint32_t m = (k.tex_modes >> (i * 5)) & 0x1F;
            bool fetch = m && m != 4 && m != 5 && m != 0xA && m != 0x11;
            if (fetch && ((k.alphakill >> i) & 1)) body += F("if (t%d.a == 0.0) discard;\n", i);
        }
        uint32_t mode0 = k.tex_modes & 0x1F;
        body += F("vec4 r0 = vec4(0.0, 0.0, 0.0, %s), r1 = vec4(0.0);\n", mode0 ? "t0.a" : "1.0");
        Ps ps{k};
        uint32_t n = std::min<uint32_t>(k.count & 0xFF, 8);
        for (uint32_t st = 0; st < n; st++) {
            ps.stage = int(st);
            ps.code += "{\n";
            std::string wc = ps.Stage(k.rgb_in[st], k.rgb_out[st], false);
            std::string wa = ps.Stage(k.a_in[st], k.a_out[st], true);
            ps.code += wc + wa + "}\n";
        }
        body += ps.code;
        if (k.final_abcd || k.final_efg) {
            ps.stage = 8;
            uint32_t fl = k.final_efg & 0xFF;
            ps.clamp_sum = fl & 0x80;
            ps.inv_v1 = fl & 0x40;
            ps.inv_r0 = fl & 0x20;
            ps.e = ps.Input(ParseIn(k.final_efg >> 24), false);
            ps.f = ps.Input(ParseIn((k.final_efg >> 16) & 0xFF), false);
            std::string A = ps.Input(ParseIn(k.final_abcd >> 24), false), B = ps.Input(ParseIn((k.final_abcd >> 16) & 0xFF), false),
                        C = ps.Input(ParseIn((k.final_abcd >> 8) & 0xFF), false), D = ps.Input(ParseIn(k.final_abcd & 0xFF), false),
                        G = ps.Input(ParseIn((k.final_efg >> 8) & 0xFF), true);
            body += "vec4 outc;\n";
            body += "outc.rgb = " + D + " + mix(" + C + ", " + B + ", " + A + ");\n";
            body += "outc.a = " + G + ";\n";
        } else {
            body += "vec4 outc = vec4(mix(fogin.rgb, r0.rgb, fogin.a), r0.a);\n";
        }
    } else {
        body += "vec4 cur = v0, tmp = vec4(0.0);\n";
        for (int i = 0; i < 4; i++) {
            const uint32_t* f = k.ff[i];
            if (f[0] <= 1) break;
            const TexInfo& t = k.tex[i];
            std::string uv = TexUv(t, i, (k.proj >> i) & 1 ? F("tc%d.xy / tc%d.w", i, i) : F("tc%d.xy", i));
            if (t.shadow)
                body += F("vec4 t%d = shadow(tex%d, %s, tc%d.z / tc%d.w, %d);\n", i, i, TexUv(t, i, F("tc%d.xy / tc%d.w", i, i)).c_str(), i, i,
                          t.shadow);
            else if (t.kind == kTex2D) body += F("vec4 t%d = texture(tex%d, %s);\n", i, i, uv.c_str());
            else if (t.kind) body += F("vec4 t%d = texture(tex%d, tc%d.xyz);\n", i, i, i);
            else body += F("vec4 t%d = vec4(1.0);\n", i);
            if ((k.alphakill >> i) & 1) body += F("if (t%d.a == 0.0) discard;\n", i);
            const char* dst = f[8] == 5 ? "tmp" : "cur";
            std::string rgb = FfOp(f[0], FfArg(f[1], i, false), FfArg(f[2], i, false), FfArg(f[3], i, false), i, false);
            std::string a = f[4] <= 1 ? std::string("cur.a") : FfOp(f[4], FfArg(f[5], i, true), FfArg(f[6], i, true), FfArg(f[7], i, true), i, true);
            body += F("{ vec3 c3 = saturate(vec3(%s)); float c1 = saturate(float(%s)); %s = vec4(c3, c1); }\n", rgb.c_str(), a.c_str(), dst);
        }
        body += "cur.rgb = saturate(cur.rgb + v1.rgb);\n";
        body += "vec4 outc = vec4(mix(fogin.rgb, cur.rgb, fogin.a), cur.a);\n";
    }
    body += "alphatest(outc.a);\noutc.rgb = saturate(outc.rgb);\noutColor = outc;\n}\n";
    return s + body;
}

}  // namespace glsl
