// OpenGL renderer: the SDK's D3D12 renderer (sdk/runtime/gpu/d3d12.cpp) at render scale 1, step for step, with the
// GL conventions of gl/glsl.cpp (console row order in every target, depth sent as 2z - w).
#include "gl/gl_backend.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <dxgiformat.h>

#include "core/log.h"
#include "gl/gl_api.h"
#include "gl/glsl.h"
#include "gpu/shaders.h"
#include "gpu/texture.h"
#include "gpu/vertex.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace glr {

using namespace gpu;

namespace {

constexpr uint64_t kStaleFrames = 8;  // frames a target may go unused and still be read back
constexpr float kDepth24Max = 16777215.0f;

float Bits(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

void Color(uint32_t argb, float* o) {
    o[0] = ((argb >> 16) & 255) / 255.0f;
    o[1] = ((argb >> 8) & 255) / 255.0f;
    o[2] = (argb & 255) / 255.0f;
    o[3] = (argb >> 24) / 255.0f;
}

uint32_t SimpleState(const State& s, uint32_t method) { return s.simple[method >> 2]; }

// D3DCMP_NEVER 0x200 to D3DCMP_ALWAYS 0x207: GL's own values, D3D12's order from 1 for the shader constants
uint32_t CmpIndex(uint32_t v) { return v >= 0x200 && v <= 0x207 ? v - 0x200 : 7; }
GLenum Cmp(uint32_t v) { return GLenum(0x200 + CmpIndex(v)); }

GLenum BlendFactor(uint32_t v) {
    switch (v) {
        case 0: case 1: case 0x300: case 0x301: case 0x302: case 0x303: case 0x304: case 0x305: case 0x306:
        case 0x307: case 0x308: case 0x8001: case 0x8002: case 0x8003: case 0x8004: return GLenum(v);
        default: return GL_ONE;
    }
}

GLenum BlendEquation(uint32_t v) {
    switch (v) {
        case 0x800A: case 0x800B: case 0x8007: case 0x8008: return GLenum(v);
        default: return GL_FUNC_ADD;
    }
}

// D3DSTENCILOP holds GL values
GLenum StencilOp(uint32_t v) {
    switch (v) {
        case 0: case 0x1E01: case 0x1E02: case 0x1E03: case 0x150A: case 0x8507: case 0x8508: return GLenum(v);
        default: return GL_KEEP;
    }
}

// D3DTADDRESS: 1 wrap, 2 mirror, 3 clamp, 4 border, 5 clamp to edge
GLint Address(uint32_t v) {
    switch (v) {
        case 2: return GL_MIRRORED_REPEAT;
        case 3: case 5: return GL_CLAMP_TO_EDGE;
        case 4: return GL_CLAMP_TO_BORDER;
        default: return GL_REPEAT;
    }
}

constexpr uint32_t kFmtD16 = 0x2C, kFmtLinD16 = 0x30;
uint8_t ShadowDepthKind(uint32_t format) { return format == kFmtD16 || format == kFmtLinD16 ? 2 : 1; }

// MemSig hashes a surface up to kSigWholeMax bytes whole, a bigger one as evenly spaced samples
constexpr uint32_t kSigWholeMax = 65536, kSigSamples = 64, kSigSampleBytes = 64;
uint64_t MemSig(uint32_t data, uint32_t total) {
    const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(data));
    uint64_t sig = Hash(&total, sizeof(total));
    if (total <= kSigWholeMax) return Hash(p, total, sig);
    for (uint32_t i = 0; i < kSigSamples; i++) sig = Hash(p + uint64_t(total - kSigSampleBytes) * i / (kSigSamples - 1), kSigSampleBytes, sig);
    return sig;
}

// a console surface the renderer draws into
struct Target {
    GLuint tex = 0;
    bool depth = false;
    uint32_t gw = 0, gh = 0;
    uint32_t data = 0, bytes = 0, pitch = 0, format = 0;  // guest memory: virtual address, size, row pitch, D3DFORMAT
    uint64_t epoch = 0, sig = 0;  // last lock seen, guest memory as last matched
    bool drawn = false;           // the GPU wrote it since the last read-back
    std::vector<uint8_t> read;    // what Sync left in guest memory, BGRA rows packed
    GLuint fb = 0;                // copy read by draws that sample the target they render to
};

// MemSig samples a big surface and misses small writes (XFONT text): after a Sync every byte is compared
bool Changed(const Target& t) {
    if (t.drawn || t.read.empty() || t.depth) return false;
    const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(t.data));
    uint32_t row = t.gw * 4;
    for (uint32_t y = 0; y < t.gh; y++)
        if (std::memcmp(p + size_t(y) * t.pitch, &t.read[size_t(y) * row], row)) return true;
    return false;
}

// a lock alone proves nothing, reads lock too: only changed bytes mean the CPU wrote it
bool LatchCpuWrite(Target& t) {
    uint64_t e = DirtyEpoch(t.data);
    if (e <= t.epoch) return false;
    t.epoch = e;
    uint64_t sig = MemSig(t.data, t.bytes);
    if (sig == t.sig && !Changed(t)) return false;
    t.sig = sig;
    return true;
}

struct Tex {
    GLuint tex = 0;
    GLenum kind = GL_TEXTURE_2D;
    TextureDesc d;
    uint32_t levels = 1;
    uint64_t sig = 0, frame = 0, palette = 0;
};

// a P8 texture's palette: its colours, their address (part of the texture's key) and their hash, all 0 without one
struct StagePalette {
    uint32_t colours[256] = {};
    uint32_t va = 0;
    uint64_t hash = 0;
};

void ReadStagePalette(const State& s, int stage, const TextureDesc& d, StagePalette& palette) {
    if (d.format != 0x0B || !s.palettes[size_t(stage)]) return;
    uint32_t header = s.palettes[size_t(stage)];
    palette.va = mem::VirtualFromPhysical(kernel::R32(header + 4));
    uint32_t size_code = kernel::R32(header) >> 30;
    std::memcpy(palette.colours, kernel::HostPtr(palette.va), (256u >> size_code) * 4);
    palette.hash = Hash(palette.colours, sizeof(palette.colours));
}

TexKind TextureKind(const TextureDesc& d) {
    if (d.cube) return kTexCube;
    if (d.depth > 1) return kTex3D;
    return kTex2D;
}

// a draw's four texture stages
struct DrawTextures {
    TexInfo info[4];
    GLuint tex[4] = {};
    GLenum kind[4] = {GL_TEXTURE_2D, GL_TEXTURE_2D, GL_TEXTURE_2D, GL_TEXTURE_2D};
    bool rt[4] = {};       // stage samples a render target
    uint32_t levels[4] = {1, 1, 1, 1};
};

// a draw's shaders
struct DrawShaders {
    bool prog = false;  // a vertex program, else the fixed function pipeline
    bool quad = false;
    bool fixed_transform = false;
    FixedKey fk;
    uint64_t vkey = 0, pkey = 0;
};

constexpr uint32_t kQuadVertices = 6;

DrawShaders ClassifyShaders(const State& s, const Assembled& a, uint32_t count) {
    DrawShaders sh;
    sh.prog = (s.vs_handle & 1) && s.vs && !s.vs->program.empty();
    bool points = a.topology == Topology::Points;
    sh.quad = count <= kQuadVertices && !s.rs[kRsZEnable] && !points;
    sh.fixed_transform = !sh.prog && !a.pretransformed;
    if (!sh.prog) sh.fk = MakeFixedKey(s);
    return sh;
}

uint64_t VertexKey(const State& s, const Assembled& a, const DrawShaders& sh) {
    if (sh.prog && !s.vs->hash) s.vs->hash = Hash(s.vs->program.data(), s.vs->program.size() * 4) | 1;
    uint64_t seed = sh.prog ? s.vs->hash ^ a.regs : Hash(&s.vs_handle, 4, a.regs | 0x10000);
    uint64_t vkey = Hash(&a.decode, 8, seed);
    if (!sh.prog && a.pretransformed) vkey ^= 0x5A5A;
    if (sh.quad) vkey ^= 0xC3C30000;
    if (!sh.prog) vkey = Hash(&sh.fk, sizeof(sh.fk), vkey);
    return vkey;
}

// table mode none under a vertex program: oFog is the blend factor, passed on as linear fog from 1 to 0
void FillFogConstants(const State& s, bool vertex_program, DrawConstants& c) {
    if (!s.rs[kRsFogEnable]) return;
    if (vertex_program && s.rs[kRsFogTableMode] == 0) {
        c.fog[0] = 1.0f;
        c.fog[1] = 0.0f;
        c.fog[2] = 0.0f;
        c.fog[3] = 3.0f;
        return;
    }
    c.fog[0] = Bits(s.rs[kRsFogStart]);
    c.fog[1] = Bits(s.rs[kRsFogEnd]);
    c.fog[2] = Bits(s.rs[kRsFogDensity]);
    c.fog[3] = float(s.rs[kRsFogTableMode]);
}

void FillSurfaceConstants(const State& s, uint32_t gw, uint32_t gh, DrawConstants& c) {
    c.surf[0] = float(gw);
    c.surf[1] = float(gh);
    c.surf[2] = 1.0f / s.zscale;
    Color(s.rs[kRsFogColor], c.fog_color);
    c.misc[0] = float(SimpleState(s, kMAlphaRef) & 0xFF);
    c.misc[1] = SimpleState(s, kMAlphaTestEnable) ? float(CmpIndex(SimpleState(s, kMAlphaFunc))) : 7.0f;
    c.misc[2] = float(CmpIndex(s.rs[kRsShadowFunc]));
    c.misc[3] = s.rs[kRsSpecularEnable] ? 1.0f : 0.0f;
}

void FillCombinerConstants(const State& s, DrawConstants& c) {
    for (int i = 0; i < 8; i++) {
        Color(SimpleState(s, kMPsConstant0 + i * 4), c.pc[i * 2]);
        Color(SimpleState(s, kMPsConstant1 + i * 4), c.pc[i * 2 + 1]);
    }
    Color(SimpleState(s, kMPsFinalConstant0), c.pc[16]);
    Color(SimpleState(s, kMPsFinalConstant1), c.pc[17]);
    Color(s.rs[kRsTextureFactor], c.tfactor);
}

void MatMul(const float* x, const float* y, float* o) {
    for (int r = 0; r < 4; r++)
        for (int col = 0; col < 4; col++)
            o[r * 4 + col] = x[r * 4] * y[col] + x[r * 4 + 1] * y[4 + col] + x[r * 4 + 2] * y[8 + col] + x[r * 4 + 3] * y[12 + col];
}

void Transpose(const float* m, float* o) {
    for (int r = 0; r < 4; r++)
        for (int col = 0; col < 4; col++) o[col * 4 + r] = m[r * 4 + col];
}

void InverseTranspose3x3(const float* wv, float* n) {
    float det = wv[0] * (wv[5] * wv[10] - wv[6] * wv[9]) - wv[1] * (wv[4] * wv[10] - wv[6] * wv[8]) + wv[2] * (wv[4] * wv[9] - wv[5] * wv[8]);
    float id = det != 0 ? 1.0f / det : 0.0f;
    for (int r = 0; r < 3; r++)
        for (int col = 0; col < 3; col++) {
            int r1 = (r + 1) % 3, r2 = (r + 2) % 3, c1 = (col + 1) % 3, c2 = (col + 2) % 3;
            n[r * 4 + col] = (wv[r1 * 4 + c1] * wv[r2 * 4 + c2] - wv[r1 * 4 + c2] * wv[r2 * 4 + c1]) * id;
        }
}

void FillTransformConstants(const State& s, uint32_t gw, uint32_t gh, DrawConstants& c) {
    float wv[16], wvp[16];
    MatMul(s.transforms[256].data(), s.transforms[2].data(), wv);
    MatMul(wv, s.transforms[3].data(), wvp);
    const auto& v = s.viewport;
    float sx = float(v.w) / gw, sy = float(v.h) / gh;
    float ox = (2.0f * v.x + v.w) / gw - 1, oy = 1 - (2.0f * v.y + v.h) / gh;
    float vpm[16] = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, v.max_z - v.min_z, 0, ox, oy, v.min_z, 1};
    float full[16];
    MatMul(wvp, vpm, full);
    Transpose(full, c.wvp);
    Transpose(wv, c.wv);
    float n[16] = {};
    InverseTranspose3x3(wv, n);
    Transpose(n, c.wvn);
    for (int t = 0; t < 4; t++) Transpose(s.transforms[16 + size_t(t)].data(), c.tm[t]);
}

// D3DLIGHT8 as floats: Type, Diffuse[4], Specular[4], Ambient[4], Position[3], Direction[3], Range, Falloff,
// Attenuation0 to 2, Theta, Phi
void LightToView(const std::array<uint32_t, 26>& l, const float* vm, float (&o)[7][4]) {
    float f[26];
    std::memcpy(f, l.data(), sizeof(f));
    for (int j = 0; j < 4; j++) {
        o[0][j] = f[1 + j];
        o[1][j] = f[5 + j];
        o[2][j] = f[9 + j];
    }
    const float *pos = f + 13, *dir = f + 16;
    for (int j = 0; j < 3; j++) {
        o[3][j] = pos[0] * vm[j] + pos[1] * vm[4 + j] + pos[2] * vm[8 + j] + vm[12 + j];
        o[4][j] = dir[0] * vm[j] + dir[1] * vm[4 + j] + dir[2] * vm[8 + j];
    }
    o[3][3] = f[19];
    o[4][3] = f[20];
    o[5][0] = f[21];
    o[5][1] = f[22];
    o[5][2] = f[23];
    o[6][0] = std::cos(f[24] * 0.5f);
    o[6][1] = std::cos(f[25] * 0.5f);
}

void FillLightConstants(const State& s, const FixedKey& fk, DrawConstants& c) {
    std::memcpy(c.mat, s.material.data(), sizeof(float) * 16);
    c.mat[4][0] = s.material[16];
    Color(s.rs[kRsAmbient], c.amb);
    const float* vm = s.transforms[2].data();
    for (int i = 0; i < 8; i++)
        if ((fk.types >> (i * 2)) & 3) LightToView(s.lights[size_t(i)], vm, c.lt[i]);
}

// a buffer written front to back, started over (orphaned) when full
// The draws' vertices, indices and constants, written once and read by the GPU once. With persistent mapping
// (GL 4.4 or ARB_buffer_storage) the buffer is mapped for good and written in place, in quarters: a fence after the
// draws of a quarter, waited for before that quarter is written again. Without it, each write maps its range
// unsynchronized and a full ring is orphaned.
struct Ring {
    static constexpr int kParts = 4;
    GLuint buf = 0;
    GLenum target = GL_ARRAY_BUFFER;
    size_t size = 0, pos = 0;
    uint64_t lap = 0;        // times round; data of this lap stays where it is
    uint8_t* map = nullptr;  // persistent
    GLsync fence[kParts] = {};
    int part = 0;

    void Init(GLenum t, size_t n, bool persistent) {
        target = t;
        Make(n, persistent);
    }
    void Make(size_t n, bool persistent) {
        size = n;
        pos = 0;
        part = 0;
        for (GLsync& f : fence)
            if (f) glDeleteSync(f), f = nullptr;
        glGenBuffers(1, &buf);
        glBindBuffer(target, buf);
        map = nullptr;
        if (persistent) {
            constexpr GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            glBufferStorage(target, GLsizeiptr(size), nullptr, flags);
            map = static_cast<uint8_t*>(glMapBufferRange(target, 0, GLsizeiptr(size), flags));
            if (map) return;
            LOG_FIRST(1, "gl: no persistent buffer mapping, mapping each write");
            glDeleteBuffers(1, &buf);
            glGenBuffers(1, &buf);
            glBindBuffer(target, buf);
        }
        glBufferData(target, GLsizeiptr(size), nullptr, GL_STREAM_DRAW);
    }
    // persistent: the quarter being left gets a fence, the next one is waited for
    void Advance() {
        if (fence[part]) glDeleteSync(fence[part]);
        fence[part] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        part = (part + 1) % kParts;
        if (fence[part]) {
            glClientWaitSync(fence[part], GL_SYNC_FLUSH_COMMANDS_BIT, 2'000'000'000ull);
            glDeleteSync(fence[part]);
            fence[part] = nullptr;
        }
    }
    // data put at `at` in lap `l` can be drawn from again
    bool Live(size_t at, uint64_t l) const { return l == lap && (!map || at / (size / kParts) == size_t(part)); }
    // where p landed
    size_t Put(const void* p, size_t n, size_t align) {
        pos = (pos + align - 1) / align * align;
        if (map) {
            if (n > size / kParts) {  // bigger than a quarter: a new, bigger ring (the old one lives while the GPU reads it)
                glDeleteBuffers(1, &buf);
                Make(std::max(size * 2, (n + align) * kParts * 2), true);
                lap++;
            } else if (pos + n > size) {
                do Advance();
                while (part != 0);
                pos = 0;
                lap++;
            }
            while (size_t(part) != (pos + n - 1) / (size / kParts)) Advance();
            std::memcpy(map + pos, p, n);
        } else {
            glBindBuffer(target, buf);
            if (n > size) {  // bigger than the ring: the ring grows
                size = n * 2;
                glBufferData(target, GLsizeiptr(size), nullptr, GL_STREAM_DRAW);
                pos = 0;
                lap++;
            } else if (pos + n > size) {
                glBufferData(target, GLsizeiptr(size), nullptr, GL_STREAM_DRAW);
                pos = 0;
                lap++;
            }
            void* m = glMapBufferRange(target, GLintptr(pos), GLsizeiptr(n), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
            if (m) {
                std::memcpy(m, p, n);
                glUnmapBuffer(target);
            } else {
                glBufferSubData(target, GLintptr(pos), GLsizeiptr(n), p);
            }
        }
        size_t at = pos;
        pos += n;
        return at;
    }
};

// GL state as the draws last left it, so a draw changes only what differs. All bytes 0xFF when unknown (an invalid
// enum, -1, NaN: never equal to what a draw wants); whatever else touches this state calls Forget.
struct StateCache {
    GLint vp[4], sc[4];
    int8_t scissor, blend, depth, stencil, poffset, cull;
    GLenum bsrc, bdst, beq;
    float bcolor[4];
    GLboolean cmask[4];
    GLenum zfunc;
    GLboolean zmask;
    GLenum sfunc;
    GLint sref;
    GLuint smask, swmask;
    GLenum sfail, szfail, spass;
    float pfactor, punits;
    GLenum front, cullface, poly;
    GLuint program;
    GLint active;
    GLuint tex[4][3];  // per unit: 2D, cube, 3D
    GLuint sampler[4];
    // the input registers' layout: registers, decodes and the elements of those registers
    struct Layout {
        uint16_t regs;
        uint64_t decode;
        VertexElement el[16];
    } layout;
    GLuint ibuf;
    GLuint ubo_buf[2];
    GLintptr ubo_at[2];

    StateCache() { Forget(); }
    void Forget() { std::memset(static_cast<void*>(this), 0xFF, sizeof(*this)); }
};

// D3D's vertex formats, as the SDK's assembler names them in DXGI terms, in GL's attribute terms. 3 component
// elements read as 4 get size 3 here: GL fills w with 1. NORMPACKED3 stays a uint for the shader to unpack.
struct AttribFormat {
    GLint size = 4;
    GLenum type = GL_FLOAT;
    GLboolean normalized = GL_FALSE;
    bool integer = false;
};
AttribFormat GlFormat(uint8_t format, uint32_t decode) {
    AttribFormat f;
    switch (format) {
        case DXGI_FORMAT_R32_FLOAT: f.size = 1; break;
        case DXGI_FORMAT_R32G32_FLOAT: f.size = 2; break;
        case DXGI_FORMAT_R32G32B32_FLOAT: f.size = 3; break;
        case DXGI_FORMAT_B8G8R8A8_UNORM: f = {GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, false}; break;
        case DXGI_FORMAT_R16_SNORM: f = {1, GL_SHORT, GL_TRUE, false}; break;
        case DXGI_FORMAT_R16G16_SNORM: f = {2, GL_SHORT, GL_TRUE, false}; break;
        case DXGI_FORMAT_R16G16B16A16_SNORM: f = {4, GL_SHORT, GL_TRUE, false}; break;
        case DXGI_FORMAT_R8_UNORM: f = {1, GL_UNSIGNED_BYTE, GL_TRUE, false}; break;
        case DXGI_FORMAT_R8G8_UNORM: f = {2, GL_UNSIGNED_BYTE, GL_TRUE, false}; break;
        case DXGI_FORMAT_R8G8B8A8_UNORM: f = {4, GL_UNSIGNED_BYTE, GL_TRUE, false}; break;
        case DXGI_FORMAT_R16_SINT: f = {1, GL_SHORT, GL_FALSE, false}; break;
        case DXGI_FORMAT_R16G16_SINT: f = {2, GL_SHORT, GL_FALSE, false}; break;
        case DXGI_FORMAT_R16G16B16A16_SINT: f = {4, GL_SHORT, GL_FALSE, false}; break;
        case DXGI_FORMAT_R32_UINT: f = {1, GL_UNSIGNED_INT, GL_FALSE, true}; break;
        default: break;  // R32G32B32A32_FLOAT, the converted vertices
    }
    if (decode == kDecodeW1 || decode == kDecodeIntW1) f.size = 3;
    return f;
}

template <typename T>
bool Changed(T& cur, T v) {
    if (cur == v) return false;
    cur = v;
    return true;
}

using Clock = std::chrono::steady_clock;
double Ms(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// where a frame's time goes, logged every few seconds: what the Switch's renderer costs
struct Perf {
    double draw = 0, assemble = 0, textures = 0, shaders = 0, upload = 0, submit = 0, present = 0, sync = 0, clear = 0, report = 0;
    uint64_t draws = 0, verts = 0, texups = 0, texup_bytes = 0, newtex = 0, programs = 0, syncs = 0, targets = 0, reports = 0;
};

GLuint Compile(GLenum type, const std::string& src) {
    GLuint sh = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        LOG_FIRST(8, "gl: %s shader failed: %s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        static int dumped = 0;
        if (dumped++ < 4) LOGE("%s", src.c_str());
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint Link(GLuint vs, GLuint fs) {
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        LOG_FIRST(8, "gl: program link failed: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// the full screen passes: an image to the screen through the gamma ramp, a depth copy
const char* kBlitFragment = R"(#version 430 core
in vec2 uv;
layout(binding = 0) uniform sampler2D img;
layout(binding = 1) uniform sampler2D lut;
uniform vec4 opts;  // x gamma, y keep alpha, z flip rows
layout(location = 0) out vec4 o;
void main() {
    vec2 t = vec2(uv.x, opts.z > 0.0 ? 1.0 - uv.y : uv.y);
    vec4 c = texture(img, t);
    if (opts.x > 0.0) c.rgb = vec3(texture(lut, vec2(c.r, 0.5)).r, texture(lut, vec2(c.g, 0.5)).g, texture(lut, vec2(c.b, 0.5)).b);
    o = vec4(c.rgb, opts.y > 0.0 ? c.a : 1.0);
}
)";
const char* kDepthFragment = R"(#version 430 core
in vec2 uv;
layout(binding = 0) uniform sampler2D img;
void main() { gl_FragDepth = texture(img, uv).r; }
)";

class GlBackend final : public Backend {
public:
    explicit GlBackend(Host* host) : host_(host) {}
    bool Init(uint32_t width, uint32_t height) override;
    void Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil) override;
    void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override;
    void Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects, uint32_t dst_points) override;
    void Present(const Surface& front) override;
    void Sync(uint32_t data, uint32_t size) override;
    void SyncLate(uint32_t data) override { Sync(data, 0); }
    void Flush() override {
        Own();
        glFlush();
    }
    void SetGamma(const uint8_t* ramp) override {
        std::memcpy(ramp_, ramp, sizeof(ramp_));
        ramp_dirty_ = true;
    }
    void ClearReport() override { counted_.clear(); }
    void GetReport(uint32_t report) override;

private:
    // targets
    uint64_t SizedTargetKey(uint32_t data, bool depth, uint32_t gw, uint32_t gh) const;
    Target* FindTarget(const Surface& s, bool depth);
    Target* EnsureTarget(const Surface& s, bool depth);
    bool TargetStale(uint32_t data) const;
    void UploadTarget(Target& t, const Surface& s);
    GLuint Framebuffer(const Target* rt, const Target* ds);
    bool BindTargets(const State& s);
    bool DepthCopy(const State& s);
    GLuint Feedback(Target& t);
    // textures
    void BindTexture(const State& s, int stage, DrawTextures& st);
    void BindGuestTexture(const State& s, int stage, const TextureDesc& d, const FormatInfo& fi, DrawTextures& st);
    void CreateTexture(Tex& tx, const TextureDesc& d, const FormatInfo& fi);
    void UploadTexture(Tex& tx, const TextureDesc& d, const FormatInfo& fi, const uint32_t* colours);
    GLuint Sampler(const State& s, int i, const DrawTextures& st);
    // shaders and state
    GLuint Program(const State& s, const Assembled& a, const DrawShaders& sh, const PixelKey& pk);
    void ApplyState(const State& s, const Assembled& a);
    void UploadConstants(const State& s, const DrawShaders& sh, const DrawTextures& st, uint32_t gw, uint32_t gh);
    void DrawGeometry(const Assembled& a);
    void FullScreen(GLuint program);
    void Blit(GLuint tex, float gamma, bool alpha, bool flip);
    void ReleaseOldTextures();
    void LogPerf();
    // the context on the calling thread
    void Own() {
        std::thread::id me = std::this_thread::get_id();
        if (owner_ == me) return;
        if (!host_->MakeCurrent()) LOG_FIRST(4, "gl: the context did not move to this thread");
        owner_ = me;
    }

    Host* host_;
    std::thread::id owner_;
    uint32_t guest_w_ = 640, guest_h_ = 480;
    std::unordered_map<uint64_t, Target> targets_;
    std::unordered_map<uint32_t, uint64_t> target_used_;
    std::map<std::pair<GLuint, GLuint>, GLuint> fbos_;
    std::unordered_map<uint64_t, Tex> textures_;
    std::unordered_map<uint64_t, GLuint> samplers_;
    std::unordered_map<uint64_t, GLuint> vshaders_, fshaders_;
    std::map<std::pair<uint64_t, uint64_t>, GLuint> programs_;
    Target* bound_rt_ = nullptr;
    Target* bound_ds_ = nullptr;
    GLuint vao_ = 0, white_ = 0, white_cube_ = 0, white_3d_ = 0, shadow_white_ = 0, lut_ = 0;
    GLuint blit_ = 0, depth_copy_ = 0;
    GLint blit_opts_ = -1;
    Ring vertices_, indices_, uniforms_;
    GLint ubo_align_ = 256;
    StateCache cache_;
    // the constants last put in the ring, drawn from again while they are the same
    std::array<std::array<float, 4>, 192> last_vsc_{};
    DrawConstants last_dc_{};
    size_t vsc_at_ = 0, dc_at_ = 0;
    uint64_t vsc_lap_ = ~0ull, dc_lap_ = ~0ull;
    Assembled asm_;
    std::vector<float> zero_vertices_;
    uint8_t ramp_[768] = {};
    bool ramp_dirty_ = false, gamma_ = false;
    uint64_t frame_ = 0;
    uint64_t frame_draws_ = 0;
    // visibility tests
    std::vector<GLuint> counted_;
    std::vector<GLuint> free_queries_;
    Perf perf_;
    Clock::time_point perf_t_ = Clock::now();
    uint64_t perf_frames_ = 0;
};

bool GlBackend::Init(uint32_t width, uint32_t height) {
    guest_w_ = width;
    guest_h_ = height;
    if (!host_->MakeCurrent()) return false;
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    LOGI("gl: %s, %s, OpenGL %d.%d, guest %ux%u", reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
         reinterpret_cast<const char*>(glGetString(GL_RENDERER)), major, minor, width, height);
    if (major * 10 + minor < 43) {
        LOGE("gl: OpenGL 4.3 is needed");
        return false;
    }
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_align_);
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    bool persistent = glBufferStorage && (major * 10 + minor >= 44 || glapi::HasExtension("GL_ARB_buffer_storage"));
    LOGI("gl: %s buffers", persistent ? "persistently mapped" : "mapped per write");
    vertices_.Init(GL_ARRAY_BUFFER, 32u << 20, persistent);
    indices_.Init(GL_ELEMENT_ARRAY_BUFFER, 8u << 20, persistent);
    uniforms_.Init(GL_UNIFORM_BUFFER, 16u << 20, persistent);
    // stages without a texture read opaque white of their kind
    uint32_t white = 0xFFFFFFFF;
    auto plain = [&](GLuint& t, GLenum kind) {
        glGenTextures(1, &t);
        glBindTexture(kind, t);
        if (kind == GL_TEXTURE_CUBE_MAP) {
            for (int f = 0; f < 6; f++) glTexImage2D(GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f), 0, GL_RGBA8, 1, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, &white);
        } else if (kind == GL_TEXTURE_3D) {
            glTexImage3D(kind, 0, GL_RGBA8, 1, 1, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, &white);
        } else {
            glTexImage2D(kind, 0, GL_RGBA8, 1, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, &white);
        }
        glTexParameteri(kind, GL_TEXTURE_MAX_LEVEL, 0);
    };
    plain(white_, GL_TEXTURE_2D);
    plain(white_cube_, GL_TEXTURE_CUBE_MAP);
    plain(white_3d_, GL_TEXTURE_3D);
    glGenTextures(1, &shadow_white_);
    glBindTexture(GL_TEXTURE_2D, shadow_white_);
    float one = 1;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 1, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &one);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    // gamma ramp
    glGenTextures(1, &lut_);
    glBindTexture(GL_TEXTURE_2D, lut_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    GLuint fsv = Compile(GL_VERTEX_SHADER, glsl::kFullScreenVertex);
    blit_ = Link(fsv, Compile(GL_FRAGMENT_SHADER, kBlitFragment));
    depth_copy_ = Link(fsv, Compile(GL_FRAGMENT_SHADER, kDepthFragment));
    if (!blit_) return false;
    blit_opts_ = glGetUniformLocation(blit_, "opts");
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    // the first call from whichever thread draws takes the context
    glFinish();
    host_->Release();
    owner_ = std::thread::id();
    return true;
}

// targets_ key of a surface's colour or depth target; a smaller depth surface laid on the depth buffer's memory is a
// target of its own
uint64_t GlBackend::SizedTargetKey(uint32_t data, bool depth, uint32_t gw, uint32_t gh) const {
    uint64_t key = uint64_t(data) << 1 | (depth ? 1 : 0);
    if (!(depth && (gw != guest_w_ || gh != guest_h_))) return key;
    return key | uint64_t(gw) << 34 | uint64_t(gh) << 48;
}

Target* GlBackend::FindTarget(const Surface& s, bool depth) {
    if (!s.header) return nullptr;
    uint32_t data = mem::VirtualFromPhysical(s.data), gw, gh;
    SurfaceSize(s, gw, gh);
    auto it = targets_.find(SizedTargetKey(data, depth, gw, gh));
    if (it == targets_.end() || it->second.gw != gw || it->second.gh != gh) return nullptr;
    target_used_[data] = frame_;
    return &it->second;
}

Target* GlBackend::EnsureTarget(const Surface& s, bool depth) {
    if (!s.header) return nullptr;
    uint32_t data = mem::VirtualFromPhysical(s.data), gw, gh;
    SurfaceSize(s, gw, gh);
    uint64_t key = SizedTargetKey(data, depth, gw, gh);
    target_used_[data] = frame_;
    auto it = targets_.find(key);
    if (it != targets_.end() && it->second.gw == gw && it->second.gh == gh) return &it->second;
    if (it != targets_.end()) {  // resized: made again
        if (bound_rt_ == &it->second || bound_ds_ == &it->second) bound_rt_ = bound_ds_ = nullptr;
        for (auto f = fbos_.begin(); f != fbos_.end();) {
            if (f->first.first == it->second.tex || f->first.second == it->second.tex) {
                glDeleteFramebuffers(1, &f->second);
                f = fbos_.erase(f);
            } else {
                ++f;
            }
        }
        glDeleteTextures(1, &it->second.tex);
        if (it->second.fb) glDeleteTextures(1, &it->second.fb);
        targets_.erase(it);
    }
    Target t;
    t.depth = depth;
    t.gw = gw;
    t.gh = gh;
    t.data = data;
    TextureDesc d;
    if (DescribeSurface(s, d)) {
        t.format = d.format;
        t.pitch = d.linear ? d.pitch : gw * (Format(d.format).bits / 8);
    }
    if (!t.pitch) t.pitch = (gw * 4 + 63) & ~63u;
    t.bytes = t.pitch * gh;
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    if (depth) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, GLsizei(gw), GLsizei(gh), 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(gw), GLsizei(gh), 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    cache_.Forget();
    perf_.targets++;
    t.epoch = DirtyEpoch(data);
    t.sig = MemSig(data, t.bytes);
    Target& ref = targets_[key] = std::move(t);
    // a new target starts black, or at the far depth
    GLuint fbo = Framebuffer(depth ? nullptr : &ref, depth ? &ref : nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glDisable(GL_SCISSOR_TEST);
    if (depth) {
        glDepthMask(GL_TRUE);
        glStencilMask(0xFF);
        glClearDepth(1.0);
        glClearStencil(0);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    } else {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    bound_rt_ = bound_ds_ = nullptr;
    return &ref;
}

GLuint GlBackend::Framebuffer(const Target* rt, const Target* ds) {
    std::pair<GLuint, GLuint> key{rt ? rt->tex : 0, ds ? ds->tex : 0};
    auto it = fbos_.find(key);
    if (it != fbos_.end()) return it->second;
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (rt) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt->tex, 0);
    if (ds) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, ds->tex, 0);
    glDrawBuffer(rt ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    glReadBuffer(rt ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) LOG_FIRST(8, "gl: framebuffer incomplete (%X) for %ux%u", st, rt ? rt->gw : ds->gw, rt ? rt->gh : ds->gh);
    fbos_[key] = fbo;
    return fbo;
}

// long unused: the title freed it and other data is at this address now
bool GlBackend::TargetStale(uint32_t data) const {
    auto it = target_used_.find(data);
    uint64_t used = it == target_used_.end() ? 0 : it->second;
    return frame_ - used > kStaleFrames;
}

// CPU pixels of a colour surface into its target
void GlBackend::UploadTarget(Target& t, const Surface& s) {
    TextureDesc d;
    if (t.depth || !Describe(s.header, d) || Format(d.format).depth || Format(d.format).compressed) return;
    std::vector<uint8_t> px;
    uint32_t pitch;
    DecodeLevel(d, 0, 0, static_cast<const uint8_t*>(kernel::HostPtr(d.data)), nullptr, px, pitch);
    cache_.Forget();
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, GLint(pitch / 4));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GLsizei(std::min(d.width, t.gw)), GLsizei(std::min(d.height, t.gh)), GL_BGRA, GL_UNSIGNED_BYTE, px.data());
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    t.epoch = DirtyEpoch(t.data);
    t.sig = MemSig(t.data, t.bytes);
    LOG_FIRST(8, "gl: CPU pixels uploaded into target %08X", t.data);
}

bool GlBackend::BindTargets(const State& s) {
    Target* rt = EnsureTarget(s.rt, false);
    Target* ds = s.rs[kRsZEnable] || s.rs[kRsStencilEnable] ? EnsureTarget(s.zs, true) : nullptr;
    if (rt && ds && (rt->gw != ds->gw || rt->gh != ds->gh)) ds = nullptr;
    if (!rt && !ds) return false;
    if (rt) {
        if (LatchCpuWrite(*rt)) UploadTarget(*rt, s.rt);
        rt->drawn = true;
    }
    if (ds) ds->drawn = true;
    glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer(rt, ds));
    bound_rt_ = rt;
    bound_ds_ = ds;
    return true;
}

// a depth buffer drawn as colour into another depth buffer's memory: copied as depth
bool GlBackend::DepthCopy(const State& s) {
    TextureDesc d;
    if (!depth_copy_ || !s.rt.header || !s.textures[0] || !Describe(s.textures[0], d) || Format(d.format).depth || d.cube || d.depth != 1) return false;
    uint32_t gw, gh;
    SurfaceSize(s.rt, gw, gh);
    auto di = targets_.find(SizedTargetKey(mem::VirtualFromPhysical(s.rt.data), true, gw, gh));
    auto si = targets_.find(SizedTargetKey(d.data, true, d.width, d.height));
    if (di == targets_.end() || si == targets_.end() || di == si) return false;
    Target &dst = di->second, &src = si->second;
    cache_.Forget();
    glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer(nullptr, &dst));
    glViewport(0, 0, GLsizei(dst.gw), GLsizei(dst.gh));
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src.tex);
    glBindSampler(0, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    FullScreen(depth_copy_);
    dst.drawn = true;
    bound_rt_ = bound_ds_ = nullptr;
    LOG_FIRST(4, "gl: depth copy %08X to %08X", src.data, dst.data);
    return true;
}

// a copy of the target to read while drawing to it
GLuint GlBackend::Feedback(Target& t) {
    if (!t.fb) {
        cache_.Forget();
        glGenTextures(1, &t.fb);
        glBindTexture(GL_TEXTURE_2D, t.fb);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(t.gw), GLsizei(t.gh), 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    }
    glCopyImageSubData(t.tex, GL_TEXTURE_2D, 0, 0, 0, 0, t.fb, GL_TEXTURE_2D, 0, 0, 0, 0, GLsizei(t.gw), GLsizei(t.gh), 1);
    return t.fb;
}

void GlBackend::CreateTexture(Tex& tx, const TextureDesc& d, const FormatInfo& fi) {
    if (tx.tex) glDeleteTextures(1, &tx.tex);
    tx = Tex{};
    tx.d = d;
    tx.levels = std::max(d.levels, 1u);
    if (fi.compressed && ((d.width & 3) || (d.height & 3))) tx.levels = 1;
    tx.kind = d.cube ? GL_TEXTURE_CUBE_MAP : d.depth > 1 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    cache_.Forget();
    glGenTextures(1, &tx.tex);
    glBindTexture(tx.kind, tx.tex);
    glTexParameteri(tx.kind, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(tx.kind, GL_TEXTURE_MAX_LEVEL, GLint(tx.levels - 1));
    perf_.newtex++;
}

void GlBackend::UploadTexture(Tex& tx, const TextureDesc& d, const FormatInfo& fi, const uint32_t* colours) {
    auto t0 = Clock::now();
    const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(d.data));
    cache_.Forget();
    glBindTexture(tx.kind, tx.tex);
    uint32_t faces = d.cube ? 6 : 1;
    std::vector<uint8_t> px;
    GLenum bc = fi.host == gpu::Host::BC1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : fi.host == gpu::Host::BC2 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
                                                                                              : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    for (uint32_t f = 0; f < faces; f++) {
        GLenum face = d.cube ? GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f) : tx.kind;
        for (uint32_t l = 0; l < tx.levels; l++) {
            uint32_t pitch;
            DecodeLevel(d, l, f, p, colours, px, pitch);
            uint32_t w = d.linear ? d.width : std::max(d.width >> l, 1u), h = d.linear ? d.height : std::max(d.height >> l, 1u);
            uint32_t z = d.linear ? 1 : std::max(d.depth >> l, 1u);
            perf_.texup_bytes += px.size();
            if (fi.compressed) {
                uint32_t bw = std::max(1u, (w + 3) / 4), bh = std::max(1u, (h + 3) / 4);
                uint32_t pw = tx.levels == 1 ? bw * 4 : w, ph = tx.levels == 1 ? bh * 4 : h;
                if (tx.kind == GL_TEXTURE_3D)
                    glCompressedTexImage3D(tx.kind, GLint(l), bc, GLsizei(pw), GLsizei(ph), GLsizei(z), 0, GLsizei(px.size()), px.data());
                else
                    glCompressedTexImage2D(face, GLint(l), bc, GLsizei(pw), GLsizei(ph), 0, GLsizei(std::min<size_t>(px.size(), size_t(pitch) * bh)), px.data());
            } else if (fi.host == gpu::Host::R32F) {
                glTexImage2D(face, GLint(l), GL_DEPTH_COMPONENT32F, GLsizei(w), GLsizei(h), 0, GL_DEPTH_COMPONENT, GL_FLOAT, px.data());
            } else if (tx.kind == GL_TEXTURE_3D) {
                glTexImage3D(tx.kind, GLint(l), GL_RGBA8, GLsizei(w), GLsizei(h), GLsizei(z), 0, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
            } else {
                glTexImage2D(face, GLint(l), GL_RGBA8, GLsizei(w), GLsizei(h), 0, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
            }
        }
    }
    perf_.texups++;
    perf_.textures += Ms(t0);
}

// a texture in guest memory: made again when its layout changed, uploaded again when its bytes did
void GlBackend::BindGuestTexture(const State& s, int stage, const TextureDesc& d, const FormatInfo& fi, DrawTextures& st) {
    TexInfo& info = st.info[stage];
    StagePalette palette;
    ReadStagePalette(s, stage, d, palette);
    uint64_t key = uint64_t(palette.va) << 32 | d.data;
    Tex& tx = textures_[key];
    bool same = tx.tex && tx.d.format == d.format && tx.d.width == d.width && tx.d.height == d.height && tx.d.depth == d.depth &&
                tx.d.levels == d.levels && tx.d.cube == d.cube && tx.d.pitch == d.pitch;
    if (!same) CreateTexture(tx, d, fi);
    // cheap change check once per frame: lock epochs plus a sample of the bytes
    if (tx.frame != frame_ + 1 || !same || tx.palette != palette.hash) {
        tx.frame = frame_ + 1;
        tx.palette = palette.hash;
        uint64_t sig = MemSig(d.data, TotalSize(d)) ^ DirtyEpoch(d.data) ^ palette.hash;
        if (sig != tx.sig || !same) {
            tx.sig = sig;
            UploadTexture(tx, d, fi, palette.va ? palette.colours : nullptr);
        }
    }
    st.tex[stage] = tx.tex;
    st.kind[stage] = tx.kind;
    st.levels[stage] = tx.levels;
    if (fi.depth) info.shadow = ShadowDepthKind(d.format);
}

void GlBackend::BindTexture(const State& s, int stage, DrawTextures& st) {
    TexInfo& info = st.info[stage];
    info = {};
    TextureDesc d;
    if (!s.textures[size_t(stage)] || !Describe(s.textures[size_t(stage)], d)) return;
    const FormatInfo& fi = Format(d.format);
    info.kind = TextureKind(d);
    info.rect = d.linear;
    // rendered by the GPU: sample the target itself; DXT data loaded over a target's memory is not it
    auto tit = targets_.find(SizedTargetKey(d.data, fi.depth, d.width, d.height));
    if (tit != targets_.end() && !d.cube && d.depth == 1 && !fi.compressed) {
        Target& t = tit->second;
        if (&t == bound_ds_) return;  // the bound depth buffer: unbound, as the D3D12 path leaves it
        st.rt[stage] = true;
        info.flags |= kTexTarget;
        info.kind = kTex2D;
        if (!t.depth && LatchCpuWrite(t)) {
            Surface su;
            su.header = s.textures[size_t(stage)];
            UploadTarget(t, su);
        }
        if (t.depth) info.shadow = ShadowDepthKind(d.format);
        // the console reads the pixels it is about to overwrite: give the draw a copy
        st.tex[stage] = &t == bound_rt_ ? Feedback(t) : t.tex;
        st.kind[stage] = GL_TEXTURE_2D;
        target_used_[t.data] = frame_;
        return;
    }
    BindGuestTexture(s, stage, d, fi, st);
}

GLuint GlBackend::Sampler(const State& s, int i, const DrawTextures& st) {
    const auto& t = s.tss[size_t(i)];
    uint32_t mag = t[kTssMagFilter], min = t[kTssMinFilter], mip = t[kTssMipFilter];
    bool shadow = st.info[i].shadow != 0;
    uint32_t key_parts[12] = {mag, min, mip, t[kTssAddressU], t[kTssAddressV], t[kTssAddressW], t[kTssMipLodBias], t[kTssMaxMipLevel],
                              t[kTssBorderColor], shadow ? s.rs[kRsShadowFunc] | 0x10000u : 0u, st.levels[i] > 1 ? 1u : 0u, 0};
    uint64_t key = Hash(key_parts, sizeof(key_parts));
    auto it = samplers_.find(key);
    if (it != samplers_.end()) return it->second;
    GLuint sm;
    glGenSamplers(1, &sm);
    auto lin = [](uint32_t f) { return f >= 2; };
    GLint fmin;
    if (st.levels[i] <= 1) fmin = lin(min) ? GL_LINEAR : GL_NEAREST;
    else if (mip == 2) fmin = lin(min) ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    else fmin = lin(min) ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    glSamplerParameteri(sm, GL_TEXTURE_MIN_FILTER, fmin);
    glSamplerParameteri(sm, GL_TEXTURE_MAG_FILTER, lin(mag) ? GL_LINEAR : GL_NEAREST);
    glSamplerParameteri(sm, GL_TEXTURE_WRAP_S, Address(t[kTssAddressU]));
    glSamplerParameteri(sm, GL_TEXTURE_WRAP_T, Address(t[kTssAddressV]));
    glSamplerParameteri(sm, GL_TEXTURE_WRAP_R, Address(t[kTssAddressW]));
    glSamplerParameterf(sm, GL_TEXTURE_LOD_BIAS, Bits(t[kTssMipLodBias]));
    glSamplerParameterf(sm, GL_TEXTURE_MIN_LOD, float(t[kTssMaxMipLevel]));
    glSamplerParameterf(sm, GL_TEXTURE_MAX_LOD, mip ? 1000.0f : float(t[kTssMaxMipLevel]));
    float border[4];
    Color(t[kTssBorderColor], border);
    glSamplerParameterfv(sm, GL_TEXTURE_BORDER_COLOR, border);
    if (shadow) {
        // the console tests texel OP r, GL tests r OP texel
        static const uint8_t swap[8] = {0, 4, 2, 6, 1, 5, 3, 7};
        glSamplerParameteri(sm, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(sm, GL_TEXTURE_COMPARE_FUNC, GLint(0x200 + swap[CmpIndex(s.rs[kRsShadowFunc])]));
        GLint f = lin(min) || lin(mag) ? GL_LINEAR : GL_NEAREST;
        glSamplerParameteri(sm, GL_TEXTURE_MIN_FILTER, f);
        glSamplerParameteri(sm, GL_TEXTURE_MAG_FILTER, f);
    }
    samplers_[key] = sm;
    return sm;
}

GLuint GlBackend::Program(const State& s, const Assembled& a, const DrawShaders& sh, const PixelKey& pk) {
    auto key = std::make_pair(sh.vkey, sh.pkey);
    auto it = programs_.find(key);
    if (it != programs_.end()) return it->second;
    auto t0 = Clock::now();
    GLuint& vs = vshaders_[sh.vkey];
    if (!vs) {
        std::string src = sh.prog ? glsl::VertexProgram(s.vs->program, a.regs, a.decode, sh.quad)
                                  : glsl::FixedVertex(a.regs, a.decode, a.pretransformed, sh.fk, sh.quad);
        vs = Compile(GL_VERTEX_SHADER, src);
    }
    GLuint& fs = fshaders_[sh.pkey];
    if (!fs) fs = Compile(GL_FRAGMENT_SHADER, glsl::Pixel(pk));
    GLuint p = Link(vs, fs);
    programs_[key] = p;
    perf_.programs++;
    perf_.shaders += Ms(t0);
    return p;
}

void GlBackend::ApplyState(const State& s, const Assembled& a) {
    StateCache& c = cache_;
    GLsizei w = GLsizei(bound_rt_ ? bound_rt_->gw : bound_ds_->gw), h = GLsizei(bound_rt_ ? bound_rt_->gh : bound_ds_->gh);
    const GLint vp[4] = {0, 0, w, h};
    if (std::memcmp(c.vp, vp, sizeof vp)) {
        glViewport(0, 0, w, h);
        std::memcpy(c.vp, vp, sizeof vp);
    }
    // the guest viewport is in the shader; the scissor clips to it, rows in console order
    int x0 = int(s.viewport.x), y0 = int(s.viewport.y), x1 = int(s.viewport.x + s.viewport.w), y1 = int(s.viewport.y + s.viewport.h);
    if (s.scissor_count) {
        x0 = std::max(x0, int(s.scissors[0]));
        y0 = std::max(y0, int(s.scissors[1]));
        x1 = std::min(x1, int(s.scissors[2]));
        y1 = std::min(y1, int(s.scissors[3]));
    }
    x0 = std::clamp(x0, 0, int(w));
    x1 = std::clamp(x1, 0, int(w));
    y0 = std::clamp(y0, 0, int(h));
    y1 = std::clamp(y1, 0, int(h));
    if (Changed(c.scissor, int8_t(1))) glEnable(GL_SCISSOR_TEST);
    const GLint sc[4] = {x0, y0, std::max(x1 - x0, 0), std::max(y1 - y0, 0)};
    if (std::memcmp(c.sc, sc, sizeof sc)) {
        glScissor(sc[0], sc[1], sc[2], sc[3]);
        std::memcpy(c.sc, sc, sizeof sc);
    }
    // blend and colour writes
    bool blend = SimpleState(s, kMBlendEnable) != 0;
    if (Changed(c.blend, int8_t(blend))) blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    if (blend) {
        GLenum src = BlendFactor(SimpleState(s, kMSrcBlend)), dst = BlendFactor(SimpleState(s, kMDestBlend));
        if (c.bsrc != src || c.bdst != dst) {
            glBlendFunc(src, dst);
            c.bsrc = src;
            c.bdst = dst;
        }
        if (Changed(c.beq, GLenum(BlendEquation(SimpleState(s, kMBlendOp))))) glBlendEquation(c.beq);
        float bf[4];
        Color(SimpleState(s, kMBlendColor), bf);
        if (std::memcmp(c.bcolor, bf, sizeof bf)) {
            glBlendColor(bf[0], bf[1], bf[2], bf[3]);
            std::memcpy(c.bcolor, bf, sizeof bf);
        }
    }
    uint32_t cw = SimpleState(s, kMColorWriteEnable);
    const GLboolean cm[4] = {GLboolean((cw >> 16) & 1), GLboolean((cw >> 8) & 1), GLboolean(cw & 1), GLboolean((cw >> 24) & 1)};
    if (std::memcmp(c.cmask, cm, sizeof cm)) {
        glColorMask(cm[0], cm[1], cm[2], cm[3]);
        std::memcpy(c.cmask, cm, sizeof cm);
    }
    // depth and stencil, on a bound depth buffer only
    bool depth = bound_ds_ && s.rs[kRsZEnable];
    if (Changed(c.depth, int8_t(depth))) depth ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    if (depth && Changed(c.zfunc, GLenum(Cmp(SimpleState(s, kMZFunc))))) glDepthFunc(c.zfunc);
    if (Changed(c.zmask, GLboolean(depth && SimpleState(s, kMZWriteEnable) ? GL_TRUE : GL_FALSE))) glDepthMask(c.zmask);
    bool stencil = bound_ds_ && s.rs[kRsStencilEnable];
    if (Changed(c.stencil, int8_t(stencil))) stencil ? glEnable(GL_STENCIL_TEST) : glDisable(GL_STENCIL_TEST);
    if (stencil) {
        GLenum f = Cmp(SimpleState(s, kMStencilFunc));
        GLint ref = GLint(SimpleState(s, kMStencilRef) & 0xFF);
        GLuint mask = SimpleState(s, kMStencilMask) & 0xFF;
        if (c.sfunc != f || c.sref != ref || c.smask != mask) {
            glStencilFunc(f, ref, mask);
            c.sfunc = f;
            c.sref = ref;
            c.smask = mask;
        }
        if (Changed(c.swmask, GLuint(SimpleState(s, kMStencilWriteMask) & 0xFF))) glStencilMask(c.swmask);
        GLenum fail = StencilOp(s.rs[kRsStencilFail]), zfail = StencilOp(SimpleState(s, kMStencilZFail)),
               pass = StencilOp(SimpleState(s, kMStencilPass));
        if (c.sfail != fail || c.szfail != zfail || c.spass != pass) {
            glStencilOp(fail, zfail, pass);
            c.sfail = fail;
            c.szfail = zfail;
            c.spass = pass;
        }
    }
    // polygon offset: bias in 24 bit depth units, slope per pixel
    bool offset = bound_ds_ && a.topology == Topology::Triangles && SimpleState(s, kMSolidOffsetEnable);
    if (Changed(c.poffset, int8_t(offset))) offset ? glEnable(GL_POLYGON_OFFSET_FILL) : glDisable(GL_POLYGON_OFFSET_FILL);
    if (offset) {
        float factor = Bits(SimpleState(s, kMSlopeScale)), units = std::round(Bits(SimpleState(s, kMDepthOffset)) * kDepth24Max / s.zscale);
        if (c.pfactor != factor || c.punits != units) {
            glPolygonOffset(factor, units);
            c.pfactor = factor;
            c.punits = units;
        }
    }
    // culling: the console's clockwise on screen is GL's counter clockwise here, the rows being kept top down.
    // D3D12's front: counter clockwise when clockwise ones are culled, else clockwise
    uint32_t cull = s.rs[kRsCullMode];
    if (Changed(c.front, GLenum(cull != 0x900 ? GL_CCW : GL_CW))) glFrontFace(c.front);
    bool culled = cull && a.topology == Topology::Triangles;
    if (Changed(c.cull, int8_t(culled))) culled ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
    if (culled && Changed(c.cullface, GLenum(GL_BACK))) glCullFace(GL_BACK);
    uint32_t fill = s.rs[kRsFillMode];
    if (Changed(c.poly, GLenum(fill == 0x1B01 ? GL_LINE : fill == 0x1B00 ? GL_POINT : GL_FILL))) glPolygonMode(GL_FRONT_AND_BACK, c.poly);
}

void GlBackend::UploadConstants(const State& s, const DrawShaders& sh, const DrawTextures& st, uint32_t gw, uint32_t gh) {
    DrawConstants c{};
    FillSurfaceConstants(s, gw, gh, c);
    FillFogConstants(s, sh.prog, c);
    FillCombinerConstants(s, c);
    for (int i = 0; i < 4; i++) {
        TextureDesc d;
        if (s.textures[size_t(i)] && Describe(s.textures[size_t(i)], d)) {
            c.texsize[i][0] = float(d.width);
            c.texsize[i][1] = float(d.height);
        }
        // the XDK's screen space offset puts the console's positions half a pixel right and down of the host's,
        // unless a program writes them without c-37: render target reads move back that far
        if (st.rt[i]) c.rtshift[i] = (!sh.prog || ReadsViewportOffset(s.vs->program)) ? -0.5f : 0.0f;
        const auto& t = s.tss[size_t(i)];
        c.bump[i][0] = Bits(t[kTssBumpEnvMat00]);
        c.bump[i][1] = Bits(t[kTssBumpEnvMat01]);
        c.bump[i][2] = Bits(t[kTssBumpEnvMat10]);
        c.bump[i][3] = Bits(t[kTssBumpEnvMat11]);
        c.bumpl[i][0] = Bits(t[kTssBumpEnvLScale]);
        c.bumpl[i][1] = Bits(t[kTssBumpEnvLOffset]);
    }
    if (sh.fixed_transform) {
        FillTransformConstants(s, gw, gh, c);
        FillLightConstants(s, sh.fk, c);
    }
    // the same constants as the last draw's are drawn from where they are
    if (!uniforms_.Live(vsc_at_, vsc_lap_) || std::memcmp(last_vsc_.data(), s.vsc.data(), sizeof(s.vsc))) {
        vsc_at_ = uniforms_.Put(s.vsc.data(), sizeof(s.vsc), size_t(ubo_align_));
        vsc_lap_ = uniforms_.lap;
        last_vsc_ = s.vsc;
    }
    if (!uniforms_.Live(dc_at_, dc_lap_) || std::memcmp(&last_dc_, &c, sizeof(c))) {
        dc_at_ = uniforms_.Put(&c, sizeof(c), size_t(ubo_align_));
        dc_lap_ = uniforms_.lap;
        last_dc_ = c;
    }
    StateCache& k = cache_;
    if (k.ubo_buf[0] != uniforms_.buf || k.ubo_at[0] != GLintptr(vsc_at_)) {
        glBindBufferRange(GL_UNIFORM_BUFFER, 0, uniforms_.buf, GLintptr(vsc_at_), sizeof(s.vsc));
        k.ubo_buf[0] = uniforms_.buf;
        k.ubo_at[0] = GLintptr(vsc_at_);
    }
    if (k.ubo_buf[1] != uniforms_.buf || k.ubo_at[1] != GLintptr(dc_at_)) {
        glBindBufferRange(GL_UNIFORM_BUFFER, 1, uniforms_.buf, GLintptr(dc_at_), sizeof(c));
        k.ubo_buf[1] = uniforms_.buf;
        k.ubo_at[1] = GLintptr(dc_at_);
    }
}

void GlBackend::DrawGeometry(const Assembled& a) {
    if (!a.regs || !a.buffer_count || (!a.index_count && !a.draw_count)) return;
    // the vertices: the title's own bytes, or the CPU's float4 copy when the input assembler cannot read them
    for (uint32_t b = 0; b < a.buffer_count; b++) {
        const VertexBuffer& vb = a.buffers[b];
        if (!vb.src || !vb.bytes || !vb.stride) return;
        size_t at = vertices_.Put(vb.src, vb.bytes, 16);
        glBindVertexBuffer(GLuint(b), vertices_.buf, GLintptr(at), GLsizei(vb.stride));
    }
    // the registers' layout stays while it is the same; only the buffers' places move
    StateCache& c = cache_;
    StateCache::Layout lay;
    std::memset(&lay, 0, sizeof lay);  // padding too: compared bytewise
    lay.regs = a.regs;
    lay.decode = a.decode;
    for (int r = 0; r < 16; r++)
        if ((a.regs >> r) & 1) lay.el[r] = a.elements[size_t(r)];
    if (std::memcmp(&lay, &c.layout, sizeof lay)) {
        for (int r = 0; r < 16; r++) {
            if ((a.regs >> r) & 1) {
                const VertexElement& e = lay.el[r];
                AttribFormat f = GlFormat(e.format, uint32_t(a.decode >> (r * 4)) & 15);
                glEnableVertexAttribArray(GLuint(r));
                if (f.integer)
                    glVertexAttribIFormat(GLuint(r), f.size, f.type, e.offset);
                else
                    glVertexAttribFormat(GLuint(r), f.size, f.type, f.normalized, e.offset);
                glVertexAttribBinding(GLuint(r), e.buffer);
            } else {
                glDisableVertexAttribArray(GLuint(r));
            }
        }
        std::memcpy(&c.layout, &lay, sizeof lay);
    }
    GLenum mode = a.topology == Topology::Points ? GL_POINTS
                  : a.topology == Topology::Lines ? (a.strip ? GL_LINE_STRIP : GL_LINES)
                                                  : (a.strip ? GL_TRIANGLE_STRIP : GL_TRIANGLES);
    if (a.index_count) {
        size_t ibytes = size_t(a.index_count) * (a.index16 ? 2 : 4);
        size_t iat = indices_.Put(a.index_src, ibytes, 4);
        if (Changed(c.ibuf, indices_.buf)) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices_.buf);
        GLenum type = a.index16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
        if (a.base_vertex)
            glDrawElementsBaseVertex(mode, GLsizei(a.index_count), type, reinterpret_cast<const void*>(iat), a.base_vertex);
        else
            glDrawElements(mode, GLsizei(a.index_count), type, reinterpret_cast<const void*>(iat));
    } else {
        glDrawArrays(mode, 0, GLsizei(a.draw_count));
    }
    perf_.verts += a.vertex_count;
}

void GlBackend::Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) {
    Own();
    auto t0 = Clock::now();
    perf_.draws++;
    frame_draws_++;
    if (DepthCopy(s)) return;
    Assembled& a = asm_;
    auto ta = Clock::now();
    // GL_CONVERTED=1: every vertex converted on the CPU, to compare
    static const bool raw = !std::getenv("GL_CONVERTED");
    bool assembled = Assemble(s, prim, start, count, indices, a, raw);
    perf_.assemble += Ms(ta);
    if (!assembled || (!a.index_count && !a.draw_count)) return;
    if (!BindTargets(s)) return;
    DrawShaders sh = ClassifyShaders(s, a, count);
    sh.vkey = VertexKey(s, a, sh);
    DrawTextures st;
    for (int i = 0; i < 4; i++) BindTexture(s, i, st);
    // a texture's copy may have moved the framebuffer binding
    glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer(bound_rt_, bound_ds_));
    PixelKey pk = MakePixelKey(s, st.info);
    pk.quad = sh.quad;
    sh.pkey = Hash(&pk, sizeof(pk));
    GLuint program = Program(s, a, sh, pk);
    if (!program) return;
    if (Changed(cache_.program, program)) glUseProgram(program);
    ApplyState(s, a);
    uint32_t gw, gh;
    SurfaceSize(bound_rt_ ? s.rt : s.zs, gw, gh);
    UploadConstants(s, sh, st, gw, gh);
    for (int i = 0; i < 4; i++) {
        const TexInfo& info = st.info[i];
        int k = info.kind == kTexCube ? 1 : info.kind == kTex3D ? 2 : 0;
        GLenum kind = k == 1 ? GL_TEXTURE_CUBE_MAP : k == 2 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
        GLuint tex = st.tex[i];
        if (!tex) tex = info.shadow ? shadow_white_ : k == 1 ? white_cube_ : k == 2 ? white_3d_ : white_;
        if (cache_.tex[i][k] != tex) {
            if (Changed(cache_.active, GLint(i))) glActiveTexture(GLenum(GL_TEXTURE0 + i));
            glBindTexture(kind, tex);
            cache_.tex[i][k] = tex;
        }
        GLuint smp = Sampler(s, i, st);
        if (Changed(cache_.sampler[i], smp)) glBindSampler(GLuint(i), smp);
    }
    // geometry, counted while the title runs a visibility test
    GLuint query = 0;
    if (s.simple[kMCountEnable >> 2]) {
        if (free_queries_.empty()) {
            free_queries_.resize(64);
            glGenQueries(64, free_queries_.data());
        }
        query = free_queries_.back();
        free_queries_.pop_back();
        glBeginQuery(GL_SAMPLES_PASSED, query);
    }
    auto tg = Clock::now();
    DrawGeometry(a);
    perf_.submit += Ms(tg);
    if (query) {
        glEndQuery(GL_SAMPLES_PASSED);
        counted_.push_back(query);
    }
    perf_.draw += Ms(t0);
}

void GlBackend::GetReport(uint32_t va) {
    Own();
    auto t0 = Clock::now();
    perf_.reports++;
    uint64_t pixels = 0;
    for (GLuint q : counted_) {
        GLuint n = 0;
        glGetQueryObjectuiv(q, GL_QUERY_RESULT, &n);
        pixels += n;
        free_queries_.push_back(q);
    }
    counted_.clear();
    kernel::W32(va, uint32_t(frame_));
    kernel::W32(va + 4, 0);
    kernel::W32(va + 8, uint32_t(std::min<uint64_t>(pixels, 0xFFFFFFFF)));
    kernel::W32(va + 12, 0);  // done
    perf_.report += Ms(t0);
}

void GlBackend::Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil) {
    Own();
    auto t0 = Clock::now();
    Target* rt = (flags & kClearTarget) ? EnsureTarget(s.rt, false) : nullptr;
    Target* ds = (flags & (kClearZ | kClearStencil)) ? EnsureTarget(s.zs, true) : nullptr;
    // pixels the CPU wrote before this clear go in first, a later upload would paint over the clear
    if (rt && LatchCpuWrite(*rt)) UploadTarget(*rt, s.rt);
    std::vector<std::array<int, 4>> area;
    if (count && rects) {
        for (uint32_t i = 0; i < count; i++) {
            int32_t r[4];
            std::memcpy(r, rects + size_t(i) * 16, 16);
            area.push_back({r[0], r[1], r[2], r[3]});
        }
    } else {
        const auto& v = s.viewport;
        area.push_back({int(v.x), int(v.y), int(v.x + v.w), int(v.y + v.h)});
    }
    cache_.Forget();
    for (Target* t : {rt, ds}) {
        if (!t) continue;
        glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer(t->depth ? nullptr : t, t->depth ? t : nullptr));
        glEnable(GL_SCISSOR_TEST);
        GLbitfield bits = 0;
        if (t->depth) {
            if (flags & kClearZ) {
                glDepthMask(GL_TRUE);
                glClearDepth(std::clamp(z, 0.0f, 1.0f));
                bits |= GL_DEPTH_BUFFER_BIT;
            }
            if (flags & kClearStencil) {
                glStencilMask(0xFF);
                glClearStencil(GLint(stencil & 0xFF));
                bits |= GL_STENCIL_BUFFER_BIT;
            }
        } else {
            float c[4];
            Color(color, c);
            // D3DCLEAR: 0x10 red, 0x20 green, 0x40 blue, 0x80 alpha
            glColorMask(flags & 0x10 ? GL_TRUE : GL_FALSE, flags & 0x20 ? GL_TRUE : GL_FALSE, flags & 0x40 ? GL_TRUE : GL_FALSE,
                        flags & 0x80 ? GL_TRUE : GL_FALSE);
            glClearColor(c[0], c[1], c[2], c[3]);
            bits = GL_COLOR_BUFFER_BIT;
        }
        for (auto& r : area) {
            int x0 = std::clamp(r[0], 0, int(t->gw)), y0 = std::clamp(r[1], 0, int(t->gh));
            int x1 = std::clamp(r[2], 0, int(t->gw)), y1 = std::clamp(r[3], 0, int(t->gh));
            if (x1 <= x0 || y1 <= y0) continue;
            glScissor(x0, y0, x1 - x0, y1 - y0);
            glClear(bits);
        }
        t->drawn = true;
    }
    bound_rt_ = bound_ds_ = nullptr;
    perf_.clear += Ms(t0);
}

void GlBackend::Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects, uint32_t dst_points) {
    Own();
    Target* src_t = FindTarget(src, false);
    // pixels the CPU made: the copy happens in memory, textures and targets pick it up from there
    if (!src_t || !src_t->drawn) {
        if (!CopySurfaceMemory(src, dst, count, src_rects, dst_points)) LOG_FIRST(8, "gl: CopyRects %08X to %08X skipped", src.data, dst.data);
        else if (Target* dst_t = FindTarget(dst, false)) UploadTarget(*dst_t, dst);
        return;
    }
    Target* dst_t = EnsureTarget(dst, false);
    if (!dst_t || src_t == dst_t) return;
    GLuint from = Framebuffer(src_t, nullptr), to = Framebuffer(dst_t, nullptr);
    cache_.Forget();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, from);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, to);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    bool listed = count && src_rects;
    uint32_t n = listed ? count : 1;
    for (uint32_t i = 0; i < n; i++) {
        int x0 = 0, y0 = 0, x1 = int(std::min(src_t->gw, dst_t->gw)), y1 = int(std::min(src_t->gh, dst_t->gh)), dx = 0, dy = 0;
        if (listed) {
            uint32_t r = src_rects + i * 16;
            x0 = int(kernel::R32(r));
            y0 = int(kernel::R32(r + 4));
            x1 = int(kernel::R32(r + 8));
            y1 = int(kernel::R32(r + 12));
            dx = x0;
            dy = y0;
            if (dst_points) {
                dx = int(kernel::R32(dst_points + i * 8));
                dy = int(kernel::R32(dst_points + i * 8 + 4));
            }
        }
        if (x1 <= x0 || y1 <= y0) continue;
        glBlitFramebuffer(x0, y0, x1, y1, dx, dy, dx + (x1 - x0), dy + (y1 - y0), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    dst_t->drawn = true;
    bound_rt_ = bound_ds_ = nullptr;
}

// the CPU reads a surface the GPU drew: bring the pixels back
void GlBackend::Sync(uint32_t data, uint32_t) {
    Own();
    auto it = targets_.find(uint64_t(data) << 1);
    if (it == targets_.end() || !it->second.drawn || TargetStale(data)) return;
    auto t0 = Clock::now();
    Target& t = it->second;
    std::vector<uint8_t> px(size_t(t.gw) * t.gh * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer(&t, nullptr));
    glReadPixels(0, 0, GLsizei(t.gw), GLsizei(t.gh), GL_BGRA, GL_UNSIGNED_BYTE, px.data());
    bound_rt_ = bound_ds_ = nullptr;
    uint8_t* guest = static_cast<uint8_t*>(kernel::HostPtr(data));
    uint32_t bpp = t.format ? Format(t.format).bits / 8 : 4;
    for (uint32_t y = 0; y < t.gh; y++) {
        uint8_t* row = guest + size_t(y) * t.pitch;
        const uint8_t* src = &px[size_t(y) * t.gw * 4];
        if (bpp == 4) {
            std::memcpy(row, src, t.gw * 4);
        } else if (bpp == 2) {  // R5G6B5
            for (uint32_t x = 0; x < t.gw; x++) {
                const uint8_t* p = src + x * 4;
                uint16_t v = uint16_t((p[2] >> 3) << 11 | (p[1] >> 2) << 5 | (p[0] >> 3));
                std::memcpy(row + x * 2, &v, 2);
            }
        }
    }
    t.read = std::move(px);
    t.drawn = false;
    t.sig = MemSig(t.data, t.bytes);
    perf_.syncs++;
    perf_.sync += Ms(t0);
    LOG_FIRST(8, "gl: target %08X (%ux%u) read back for the CPU", data, t.gw, t.gh);
}

void GlBackend::FullScreen(GLuint program) {
    cache_.Forget();
    glUseProgram(program);
    for (int r = 0; r < 16; r++) glDisableVertexAttribArray(GLuint(r));
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void GlBackend::Blit(GLuint tex, float gamma, bool alpha, bool flip) {
    cache_.Forget();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindSampler(0, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, lut_);
    glBindSampler(1, 0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(blit_);
    glUniform4f(blit_opts_, gamma, alpha ? 1.0f : 0.0f, flip ? 1.0f : 0.0f, 0.0f);
    FullScreen(blit_);
}

void GlBackend::ReleaseOldTextures() {
    if (frame_ % 60 != 0 || textures_.size() <= 1024) return;
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (it->second.frame + 600 < frame_) {
            glDeleteTextures(1, &it->second.tex);
            it = textures_.erase(it);
        } else {
            ++it;
        }
    }
}

void GlBackend::LogPerf() {
    perf_frames_++;
    double el = Ms(perf_t_);
    if (el < 5000) return;
    double n = double(perf_frames_);
    const Perf& p = perf_;
    LOGI("perf: %.1f fps, %.1f ms/frame: draws %.1f (assemble %.1f, textures %.1f, shaders %.1f, submit %.1f), clear %.1f, "
         "sync %.1f, reports %.1f, present %.1f | per frame %.0f draws %.0fk verts %.1f tex uploads %.2f MB %.1f reports | "
         "%llu new textures %llu programs %llu targets %llu read-backs in %.0f s",
         n * 1000 / el, el / n, p.draw / n, p.assemble / n, p.textures / n, p.shaders / n, p.submit / n, p.clear / n, p.sync / n,
         p.report / n, p.present / n, p.draws / n, p.verts / n / 1000, p.texups / n, p.texup_bytes / n / 1048576.0, p.reports / n,
         (unsigned long long)p.newtex, (unsigned long long)p.programs, (unsigned long long)p.targets, (unsigned long long)p.syncs,
         el / 1000);
    perf_ = Perf{};
    perf_t_ = Clock::now();
    perf_frames_ = 0;
}

// GLDUMP=n: every colour target as frame n leaves them, as gltarget_<frame>_<address>_<w>x<h>.tga beside the log
void DumpTargets(std::unordered_map<uint64_t, Target>& targets, uint64_t frame, GLuint (*fbo)(void*, const Target*), void* self) {
    for (auto& [key, t] : targets) {
        if (t.depth) continue;
        std::vector<uint8_t> px(size_t(t.gw) * t.gh * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo(self, &t));
        glReadPixels(0, 0, GLsizei(t.gw), GLsizei(t.gh), GL_BGRA, GL_UNSIGNED_BYTE, px.data());
        char name[96];
        std::snprintf(name, sizeof name, "cap/gltarget_%05llu_%08X_%ux%u.tga", (unsigned long long)frame, t.data, t.gw, t.gh);
        FILE* f = std::fopen(name, "wb");
        if (!f) continue;
        uint8_t head[18] = {0, 0, 2};
        head[12] = uint8_t(t.gw), head[13] = uint8_t(t.gw >> 8), head[14] = uint8_t(t.gh), head[15] = uint8_t(t.gh >> 8);
        head[16] = 32;
        head[17] = 0x28;  // top row first, 8 alpha bits
        std::fwrite(head, 1, 18, f);
        std::fwrite(px.data(), 1, px.size(), f);
        std::fclose(f);
    }
}

void GlBackend::Present(const Surface& front) {
    Own();
    auto t0 = Clock::now();
    static const long dump = std::getenv("GLDUMP") ? std::atol(std::getenv("GLDUMP")) : -1;
    if (long(frame_) == dump)
        DumpTargets(targets_, frame_, [](void* self, const Target* t) { return static_cast<GlBackend*>(self)->Framebuffer(t, nullptr); }, this);
    Target* t = EnsureTarget(front, false);
    if (t && LatchCpuWrite(*t)) UploadTarget(*t, front);
    if (ramp_dirty_) {
        ramp_dirty_ = false;
        uint8_t px[256 * 4];
        bool identity = true;
        for (int i = 0; i < 256; i++) {
            px[i * 4] = ramp_[512 + i];
            px[i * 4 + 1] = ramp_[256 + i];
            px[i * 4 + 2] = ramp_[i];
            px[i * 4 + 3] = 255;
            identity &= ramp_[i] == i && ramp_[256 + i] == i && ramp_[512 + i] == i;
        }
        gamma_ = !identity;
        glBindTexture(GL_TEXTURE_2D, lut_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_BGRA, GL_UNSIGNED_BYTE, px);
    }
    uint32_t sw = 1280, sh = 720;
    host_->ScreenSize(sw, sh);
    glBindFramebuffer(GL_FRAMEBUFFER, host_->ScreenFramebuffer());
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, GLsizei(sw), GLsizei(sh));
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (t) {
        // 4:3 at the screen's height, black bars at the sides
        float vw = std::min(float(sw), sh * 4.0f / 3.0f), vh = vw * 3.0f / 4.0f;
        glViewport(GLint((sw - vw) / 2), GLint((sh - vh) / 2), GLsizei(vw), GLsizei(vh));
        Blit(t->tex, gamma_ ? 1.0f : 0.0f, false, true);
    }
    bound_rt_ = bound_ds_ = nullptr;
    host_->Presented(uint32_t(frame_draws_));
    cache_.Forget();  // the host may have drawn too
    perf_.present += Ms(t0);
    host_->Swap();
    frame_++;
    frame_draws_ = 0;
    ReleaseOldTextures();
    LogPerf();
}

}  // namespace

std::unique_ptr<Backend> CreateBackend(Host* host) { return std::make_unique<GlBackend>(host); }

void DrawImage(const uint32_t* rgba, uint32_t w, uint32_t h, uint32_t screen_w, uint32_t screen_h, bool upload) {
    static GLuint program = 0, tex = 0, vao = 0;
    static GLint opts = -1;
    static uint32_t tw = 0, th = 0;
    if (!program) {
        program = Link(Compile(GL_VERTEX_SHADER, glsl::kFullScreenVertex), Compile(GL_FRAGMENT_SHADER, kBlitFragment));
        if (!program) return;
        opts = glGetUniformLocation(program, "opts");
        glGenTextures(1, &tex);
        glGenVertexArrays(1, &vao);
    }
    GLint old_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);
    glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindSampler(0, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (w != tw || h != th) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(w), GLsizei(h), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        tw = w;
        th = h;
    } else if (upload) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GLsizei(w), GLsizei(h), GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, GLsizei(screen_w), GLsizei(screen_h));
    glUseProgram(program);
    glUniform4f(opts, 0.0f, 0.0f, 1.0f, 0.0f);  // rows flipped onto the window
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(GLuint(old_vao));
}

}  // namespace glr
