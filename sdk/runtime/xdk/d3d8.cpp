// Direct3D 8 (XDK 5849) entry points the title calls, implemented on the host renderer.
// Resource creation, locking and the XG helpers stay recompiled: they only manage memory.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "app/app.h"
#include "core/diag.h"
#include "gpu/backend.h"
#include "gpu/shaders.h"
#include "gpu/texture.h"
#include "gpu/vertex.h"
#include "xdk/d3d8.h"
#include "xdk/hle.h"
#include "xdk/layout.h"

using namespace kernel;

// configs written before the SDK knew the name
#ifndef SYM_D3DDevice_BeginPush
#define SYM_D3DDevice_BeginPush FFFFFE00
#define HAS_D3DDevice_BeginPush 0
#endif
#ifndef SYM_D3DDevice_EndPush
#define SYM_D3DDevice_EndPush FFFFFE01
#define HAS_D3DDevice_EndPush 0
#endif
#ifndef HAS_D3D_FvfShader
#define HAS_D3D_FvfShader 0
#endif
#ifndef SYM_D3DDevice_SetPalette
#define SYM_D3DDevice_SetPalette FFFFFE02
#define HAS_D3DDevice_SetPalette 0
#endif
#ifndef SYM_D3DDevice_GetVertexShaderFunction
#define SYM_D3DDevice_GetVertexShaderFunction FFFFFE03
#define HAS_D3DDevice_GetVertexShaderFunction 0
#endif
#ifndef SYM_D3DDevice_LoadVertexShaderProgram
#define SYM_D3DDevice_LoadVertexShaderProgram FFFFFE04
#define HAS_D3DDevice_LoadVertexShaderProgram 0
#endif
#ifndef SYM_D3DDevice_SetVertexShaderInput
#define SYM_D3DDevice_SetVertexShaderInput FFFFFE05
#define HAS_D3DDevice_SetVertexShaderInput 0
#endif
#ifndef SYM_D3DDevice_RunPushBuffer
#define SYM_D3DDevice_RunPushBuffer FFFFFE06
#define HAS_D3DDevice_RunPushBuffer 0
#endif

namespace gpu {
// A/B switch: 0 draws on the title's thread, as before
#ifndef XBE_RENDER_THREAD
#define XBE_RENDER_THREAD 1
#endif
bool g_render_thread = XBE_RENDER_THREAD;
}  // namespace gpu

namespace d3d {

namespace {

// D3D globals of this build
constexpr uint32_t kDevice = SYM(D3D_Device);        // the static CDevice, g_pDevice points at it
constexpr uint32_t kDevicePtr = SYM(D3D_pDevice);
constexpr uint32_t kIndexData = SYM(D3D_IndexData);  // index data base the inline draws read
constexpr uint32_t kDirtyFlags = SYM(D3D_DirtyFlags);
constexpr uint32_t kTextureState = SYM(D3D_DeferredTextureState);  // [4][32]
constexpr uint32_t kRenderState = SYM(D3D_RenderState);            // [166]
constexpr uint32_t kStreams = SYM(D3D_Streams);                    // [16] stride, offset, buffer
constexpr uint32_t kSimpleEncode = SYM(D3D_SimpleRenderStateEncode);

// CDevice fields mirrored so the recompiled getters and resource code see what the XDK would
constexpr uint32_t kDevPush = XDK_CDevice_Push, kDevThreshold = XDK_CDevice_PushThreshold,
                   kDevBaseVertex = XDK_CDevice_BaseVertex, kDevIndexBuffer = XDK_CDevice_IndexBuffer,
                   kDevTextures = XDK_CDevice_Textures, kDevRenderTarget = XDK_CDevice_RenderTarget,
                   kDevDepth = XDK_CDevice_Depth, kDevSwapCount = XDK_CDevice_SwapCount,
                   kDevBackBuffer = XDK_CDevice_BackBuffer, kDevFrontBuffer = XDK_CDevice_FrontBuffer,
                   kDevZScale = XDK_CDevice_ZScale, kDevSuperX = XDK_CDevice_SuperSampleX,
                   kDevSuperY = XDK_CDevice_SuperSampleY, kDevTransforms = XDK_CDevice_Transforms,
                   kDevPalettes = XDK_CDevice_Palettes;
constexpr uint32_t kStageStates = XDK_TextureState_Stage, kStreamSize = XDK_Stream_Size;

gpu::State g_state;
std::unique_ptr<gpu::Backend> g_backend;
std::recursive_mutex g_lock;

std::unordered_map<uint32_t, std::shared_ptr<gpu::VertexShader>> g_vs;
std::unordered_map<uint32_t, std::shared_ptr<gpu::PixelShader>> g_ps;
std::array<uint32_t, 3> g_buffers{};  // front, back surfaces
uint32_t g_buffer_count = 2;
uint32_t g_depth = 0;
uint32_t g_scratch_push = 0, g_scratch_size = 0;
uint32_t g_walk = 0;  // first scratch command not read yet

uint32_t Dev(uint32_t off) { return R32(kDevice + off); }
void SetDev(uint32_t off, uint32_t v) { W32(kDevice + off, v); }

// The device's own references: a count in bits 19 to 22 of the resource's first dword. A surface's first one
// also holds its parent. The library's functions do it when the image has them, inlined ones are done here.
void AddRef(X86Context& ctx, uint32_t res) {
    if (!res) return;
#if HAS_D3D_InternalAddRef
    GUEST_CALL(D3D_InternalAddRef, ctx, {res});
#else
    (void)ctx;
    uint32_t common = R32(res);
    if (!(common & 0x780000) && (common & 0x70000) == 0x50000)
        if (uint32_t parent = R32(res + 0x14)) W32(parent, R32(parent) + 0x80000);
    W32(res, R32(res) + 0x80000);
#endif
}
void Release(X86Context& ctx, uint32_t res) {
    if (!res) return;
#if HAS_D3D_InternalRelease
    GUEST_CALL(D3D_InternalRelease, ctx, {res});
#else
    (void)ctx;
    // the library frees a resource nothing holds any more; without its function the memory stays
    auto drop = [](uint32_t r) {
        W32(r, R32(r) - 0x80000);
        if (!(R32(r) & 0x78FFFF)) LOG_FIRST(8, "d3d: resource %08X is not freed, the image has no D3D_InternalRelease", r);
    };
    uint32_t common = R32(res);
    if ((common & 0x780000) == 0x80000 && (common & 0x70000) == 0x50000)
        if (uint32_t parent = R32(res + 0x14)) drop(parent);
    drop(res);
#endif
}

void Walk(uint32_t end);

// Hardware commands a caller still pushes go to a scratch buffer, rewound at every request once Walk has read it
uint32_t ScratchPush(uint32_t bytes = 0) {
    Walk(Dev(kDevPush));
    uint32_t need = std::max<uint32_t>(0x10000, (bytes + 0x1FFF) & ~0xFFFu);
    if (need > g_scratch_size) {
        if (g_scratch_push) mem::FreeContiguous(g_scratch_push);
        g_scratch_push = mem::AllocContiguous(need, 0, 0xFFFFFFFF, 0x1000);
        g_scratch_size = need;
    }
    SetDev(kDevPush, g_scratch_push);
    SetDev(kDevThreshold, g_scratch_push + g_scratch_size - 0x1000);
    g_walk = g_scratch_push;
    return g_scratch_push;
}

gpu::Surface SurfaceOf(uint32_t header) {
    gpu::Surface s;
    s.header = header;
    if (header) {
        s.data = R32(header + 4);
        s.format = R32(header + 12);
        s.size = R32(header + 16);
    }
    return s;
}

uint32_t NewSurface(uint32_t w, uint32_t h, uint32_t fmt, uint32_t bpp) {
    uint32_t pitch = (w * bpp + 63) & ~63u;
    uint32_t data = mem::AllocContiguous(pitch * h, 0, 0xFFFFFFFF, 0x1000);
    uint32_t hdr = mem::AllocSystem(24);
    W32(hdr + 0, 0x000D0001);  // surface, one reference, one internal reference held by the device
    W32(hdr + 4, mem::PhysicalFromVirtual(data));
    W32(hdr + 8, 0);
    W32(hdr + 12, 0x00010021 | (fmt << 8));  // dma A, 2D, one level
    W32(hdr + 16, (w - 1) | (h - 1) << 12 | (pitch / 64 - 1) << 24);
    W32(hdr + 20, 0);
    return hdr;
}

void SyncDeferred() {
    // deferred states live in the title's memory, the inline SetRenderState and SetTextureStageState write them
    for (uint32_t i = gpu::kRsDeferredFirst; i < gpu::kRsComplexFirst; i++) g_state.rs[i] = R32(kRenderState + i * 4);
    for (int s = 0; s < 4; s++)
        for (uint32_t i = 0; i < gpu::kTssBumpEnvMat00; i++) g_state.tss[s][i] = R32(kTextureState + (s * kStageStates + i) * 4);
}

float ZScale(const gpu::Surface& zs) {
    uint32_t f = zs.header ? (zs.format >> 8) & 0xFF : 0x2E;
    return f == 0x2C || f == 0x30 ? 65535.0f : 16777215.0f;
}

bool g_own_constants = false;  // D3DSCM_NORESERVEDCONSTANTS: c-38 and c-37 are the title's

// c-38 and c-37 hold the viewport scale and offset, as D3D_GetViewportOffsetAndScale computes them
void UpdateViewportConstants() {
    auto& v = g_state.viewport;
    g_state.zscale = ZScale(g_state.zs);
    float hw = v.w * 0.5f, hh = v.h * 0.5f;
    if (!g_own_constants) {
        g_state.vsc[58] = {hw, -hh, (v.max_z - v.min_z) * g_state.zscale, 0};
        g_state.vsc[59] = {v.x + g_state.ss_offset_x + hw, v.y + g_state.ss_offset_y + hh, v.min_z * g_state.zscale, 0};
    }
    // the viewport as the library's SetViewport stores it, for inlined readers
    if (XDK_CDevice_Viewport != xdk::kUnknown) {
        uint32_t p = kDevice + XDK_CDevice_Viewport;
        W32(p, v.x);
        W32(p + 4, v.y);
        W32(p + 8, v.w);
        W32(p + 12, v.h);
        std::memcpy(HostPtr(p + 16), &v.min_z, 4);
        std::memcpy(HostPtr(p + 20), &v.max_z, 4);
    }
}

void SetComplex(uint32_t rs, uint32_t v) {
    g_state.rs[rs] = v;
    W32(kRenderState + rs * 4, v);
}

// The library's default render and texture stage states; its pushes land in the scratch buffer
void ApplyDefaultStates(X86Context& ctx) {
#if HAS_D3D_InitDefaultState
    GUEST_CALL(D3D_InitDefaultState, ctx, {});
#else
    (void)ctx;
    LOG_FIRST(1, "d3d: the image has no D3D_InitDefaultState, default states were not applied");
#endif
}

constexpr bool kVBlank = XDK_CDevice_VBlankCount != xdk::kUnknown && XDK_CDevice_VBlankEvent != xdk::kUnknown &&
                         XDK_CDevice_VBlankCallback != xdk::kUnknown;

// what the library's vertical blank interrupt leaves for the title: count, event, callback
void VerticalBlank(X86Context& ctx) {
    using clock = std::chrono::steady_clock;
    uint32_t info = mem::AllocSystem(12), shown = 0;
    auto next = clock::now();
    for (;;) {
        next = std::max(next + std::chrono::microseconds(16667), clock::now());
        std::this_thread::sleep_until(next);
        uint32_t count = Dev(XDK_CDevice_VBlankCount) + 1, swaps = Dev(kDevSwapCount);
        SetDev(XDK_CDevice_VBlankCount, count);
        ObjectRef event = FromPointer(kDevice + XDK_CDevice_VBlankEvent);
        SetEvent(*event);
        if (uint32_t callback = Dev(XDK_CDevice_VBlankCallback)) {
            // blank count, swaps shown, 1 when this blank showed one
            W32(info, count);
            W32(info + 4, swaps);
            W32(info + 8, swaps != shown);
            CallGuest(ctx, callback, {info});
        }
        shown = swaps;
        // the title clears the event in its own memory before a wait, which the host object does not see
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ResetEvent(*event);
    }
}

}  // namespace

gpu::State& State() { return g_state; }
std::recursive_mutex& Lock() { return g_lock; }

}  // namespace d3d

using namespace d3d;

// device

HLE_STDCALL(Direct3D_CreateDevice, 6) {
    XDK_REQUIRE("d3d", XDK_DATA(D3D_Device), XDK_DATA(D3D_pDevice), XDK_DATA(D3D_DirtyFlags),
                XDK_DATA(D3D_DeferredTextureState), XDK_DATA(D3D_RenderState), XDK_DATA(D3D_Streams),
                XDK_DATA(D3D_SimpleRenderStateEncode));
    XDK_REQUIRE("d3d", XDK_KEY(CDevice_Push), XDK_KEY(CDevice_PushThreshold), XDK_KEY(CDevice_BaseVertex),
                XDK_KEY(CDevice_IndexBuffer), XDK_KEY(CDevice_ZScale), XDK_KEY(CDevice_SuperSampleX),
                XDK_KEY(CDevice_SuperSampleY), XDK_KEY(CDevice_Transforms), XDK_KEY(CDevice_Textures),
                XDK_KEY(CDevice_RenderTarget), XDK_KEY(CDevice_Depth), XDK_KEY(CDevice_BackBuffer),
                XDK_KEY(CDevice_FrontBuffer), XDK_KEY(CDevice_SwapCount), XDK_KEY(CDevice_Size),
                XDK_KEY(DirtyFlags_Texture), XDK_KEY(TextureState_Stage), XDK_KEY(Stream_Size),
                XDK_KEY(Stream_Stride), XDK_KEY(Stream_Buffer));
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t pp = a[4];  // D3DPRESENT_PARAMETERS
    uint32_t w = R32(pp), h = R32(pp + 4), fmt = R32(pp + 8), count = std::max<uint32_t>(R32(pp + 12), 1);
    uint32_t auto_depth = R32(pp + 0x20), depth_fmt = R32(pp + 0x24), interval = R32(pp + 0x30);
    if (!w) w = 640;
    if (!h) h = 480;
    LOGI("d3d: CreateDevice %ux%u format %X buffers %u depth %u format %X interval %X flags %X", w, h, fmt, count,
         auto_depth, depth_fmt, interval, R32(pp + 0x28));
    std::memset(HostPtr(kDevice), 0, XDK_CDevice_Size);
    W32(kDevicePtr, kDevice);
    ScratchPush();

    uint32_t color_fmt = gpu::LinearColorFormat(fmt);
    g_buffer_count = std::min<uint32_t>(count + 1, 3);
    for (uint32_t i = 0; i < g_buffer_count; i++) g_buffers[i] = NewSurface(w, h, color_fmt, 4);
    g_depth = 0;
    if (auto_depth) {
        uint32_t zf = gpu::LinearDepthFormat(depth_fmt);
        g_depth = NewSurface(w, h, zf, zf == 0x30 || zf == 0x31 ? 2 : 4);  // 0x30 LIN_D16, 0x31 LIN_F16
    }
    if (!g_backend) {
        g_backend = gpu::CreateBackend();
        if (!g_backend->Init(w, h)) LOGE("d3d: renderer init failed");
        if (gpu::g_render_thread) {
            LOGI("d3d: the renderer runs on a thread of its own");
            g_backend = gpu::CreateThreadedBackend(std::move(g_backend));
        }
    }
    g_state = gpu::State{};
    g_state.viewport = {0, 0, w, h, 0.0f, 1.0f};
    SetDev(kDevRenderTarget, g_buffers[1]);
    SetDev(kDevDepth, g_depth);
    SetDev(kDevBackBuffer, g_buffers[1]);
    SetDev(kDevFrontBuffer, g_buffers[1]);
    float one = 1.0f, zs = g_depth && (R32(g_depth + 12) >> 8 & 0xFF) == 0x30 ? 65535.0f : 16777215.0f;
    std::memcpy(HostPtr(kDevice + kDevSuperX), &one, 4);
    std::memcpy(HostPtr(kDevice + kDevSuperY), &one, 4);
    std::memcpy(HostPtr(kDevice + kDevZScale), &zs, 4);
    // fields the library's init sets that inlined library code and the default states read
    if (XDK_CDevice_AutoDepth != xdk::kUnknown) SetDev(XDK_CDevice_AutoDepth, g_depth);
    if (XDK_CDevice_BackBufferScale != xdk::kUnknown) {
        std::memcpy(HostPtr(kDevice + XDK_CDevice_BackBufferScale), &one, 4);
        std::memcpy(HostPtr(kDevice + XDK_CDevice_BackBufferScale + 4), &one, 4);
    }
    // 60 Hz, widescreen
    if (XDK_CDevice_DisplayFlags != xdk::kUnknown) SetDev(XDK_CDevice_DisplayFlags, 0x400000 | (app::Widescreen() ? 0x10000 : 0));
#if HAS_D3D_FvfShader
    if (XDK_CDevice_VertexShader != xdk::kUnknown) SetDev(XDK_CDevice_VertexShader, SYM(D3D_FvfShader));
#endif
    if (kVBlank) {
        // notification event, signalled
        SetDev(XDK_CDevice_VBlankEvent, 0x00040000);
        SetDev(XDK_CDevice_VBlankEvent + 4, 1);
        static bool started = false;
        if (!started) StartHostThread("vertical blank", VerticalBlank);
        started = true;
    }
    g_state.rt = SurfaceOf(g_buffers[1]);
    g_state.zs = SurfaceOf(g_depth);
    UpdateViewportConstants();
    ApplyDefaultStates(ctx);
    if (a[5]) W32(a[5], kDevice);
    return 0;
}

HLE_STDCALL(D3DDevice_Release, 0) { return 1; }

// synchronisation, the renderer never runs behind

HLE_STDCALL(D3D_BlockOnResource, 1) {
    uint32_t res = a[0];
    if (res && g_backend) {
        std::lock_guard<std::recursive_mutex> lock(g_lock);
        uint32_t type = R32(res) & 0x70000;  // 0x40000 texture, 0x50000 surface
        if (type == 0x40000 || type == 0x50000) {
            uint32_t data = mem::VirtualFromPhysical(R32(res + 4));
            // perf diagnostic: who blocks on which surface, once each
            static std::unordered_set<uint32_t> seen;
            if (diag::Perf() && seen.insert(data).second) {
                uint32_t code[4] = {}, n = 0;
                for (uint32_t i = 0; i < 32 && n < 4; i++) {
                    uint32_t v = R32(ctx.esp + i * 4);
                    if (v >= X86CodeBase && v < X86CodeEnd) code[n++] = v;
                }
                LOGI("d3d: BlockOnResource on surface %08X, stack %08X %08X %08X %08X", data, code[0], code[1], code[2], code[3]);
            }
            g_backend->Sync(data, 0);
        } else {
            g_backend->WaitResource(res);
        }
    }
    gpu::MarkDirty(res);
    return 0;
}
// lock(resource, face, level, locked, rect, flags): D3DLOCK_NOOVERWRITE skips the block above, yet a
// title may read what the GPU drew
HLE_WRAP(Lock2DSurface, 6) {
    uint32_t res = a[0];
    if ((a[5] & 0x20) && res && g_backend) {  // D3DLOCK_NOOVERWRITE
        std::lock_guard<std::recursive_mutex> lock(g_lock);
        g_backend->SyncLate(mem::VirtualFromPhysical(R32(res + 4)));
    }
}
HLE_STDCALL(D3DResource_BlockUntilNotBusy, 1) {
    if (a[0] && g_backend) {
        std::lock_guard<std::recursive_mutex> lock(g_lock);
        g_backend->WaitResource(a[0]);
    }
    gpu::MarkDirty(a[0]);
    return 0;
}
HLE_STDCALL(D3DResource_IsBusy, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t res = a[0];
    if (!res) return 0;
    // set on the device, a surface also through its texture
    uint32_t common = R32(res), parent = (common & 0x70000) == 0x50000 ? R32(res + 0x14) : 0;
    if ((common & 0x780000) || (parent && (R32(parent) & 0x780000))) return 1;  // internal reference count
    return g_backend && g_backend->Busy(res) ? 1 : 0;
}
HLE_STDCALL(D3D_BlockOnTime, 2) { return 0; }
HLE_STDCALL(D3DDevice_BlockOnFence, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (g_backend) g_backend->WaitFence(a[0]);
    return 0;
}
HLE_STDCALL(D3DDevice_InsertFence, 0) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    return g_backend ? g_backend->Fence() : 0;
}
HLE_STDCALL(D3DDevice_IsFencePending, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    return g_backend && g_backend->FencePending(a[0]) ? 1 : 0;
}
HLE_STDCALL(D3D_SetFence, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    return g_backend ? g_backend->Fence() : 0;
}
HLE_STDCALL(D3D_KickOffAndWaitForIdle, 0) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (g_backend) g_backend->Flush();
    return 0;
}
HLE_FASTCALL(CDevice_KickOff, 0) { return 0; }

HLE_STDCALL(D3DDevice_MakeSpace, 0) {
    LOG_FIRST(16, "d3d: MakeSpace from %08X, some caller still pushes hardware commands", R32(ctx.esp));
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    return ScratchPush();
}
HLE_STDCALL(D3D_MakeRequestedSpace, 2) {
    LOG_FIRST(16, "d3d: MakeRequestedSpace from %08X", R32(ctx.esp));
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    return ScratchPush();
}

HLE_STDCALL(D3DDevice_BlockUntilVerticalBlank, 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
    return 0;
}
HLE_STDCALL(D3DDevice_SetFlickerFilter, 1) { return 0; }
HLE_STDCALL(D3DDevice_SetSoftDisplayFilter, 1) { return 0; }
HLE_STDCALL(D3DDevice_EnableOverlay, 1) { return 0; }
// the front buffer, read back at the guest size, is kept for the next instance after a quick reboot, which may
// fade from it at its first frames
HLE_STDCALL(D3DDevice_PersistDisplay, 0) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t front = g_buffers[0], size = R32(front + 0x10);
    uint32_t pitch = ((size >> 24 & 0xFF) + 1) * 64, height = (size >> 12 & 0xFFF) + 1, bytes = pitch * height;
    uint32_t data = mem::VirtualFromPhysical(R32(front + 4));
    if (g_backend) g_backend->Sync(data, bytes);
    kernel::SavePersistedDisplay(R32(front), R32(front + 0xC), size, HostPtr(data), bytes);
    LOGI("d3d: PersistDisplay, %u bytes", bytes);
    return 0;
}
HLE_STDCALL(D3DDevice_FlushVertexCache, 0) { return 0; }

HLE_STDCALL(D3DDevice_SetGammaRamp, 2) {
    uint8_t ramp[768];
    std::memcpy(ramp, HostPtr(a[1]), 768);
    if (g_backend) g_backend->SetGamma(ramp);
    return 0;
}

// targets and presentation

HLE_STDCALL(D3DDevice_SetRenderTarget, 2) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t rt = a[0] ? a[0] : Dev(kDevRenderTarget), zs = a[1];
    AddRef(ctx, rt);
    uint32_t old_rt = Dev(kDevRenderTarget), old_zs = Dev(kDevDepth);
    SetDev(kDevRenderTarget, rt);
    SetDev(kDevDepth, zs);
    AddRef(ctx, zs);
    Release(ctx, old_rt);
    Release(ctx, old_zs);
    g_state.rt = SurfaceOf(rt);
    g_state.zs = SurfaceOf(zs);
    // SetRenderTarget resets the viewport to the whole target
    uint32_t w, h;
    gpu::SurfaceSize(g_state.rt, w, h);
    g_state.viewport = {0, 0, w, h, 0.0f, 1.0f};
    UpdateViewportConstants();
    return 0;
}

HLE_STDCALL(D3DDevice_GetBackBuffer2, 1) {
    int32_t i = int32_t(a[0]);
    uint32_t s = i < 0 ? g_buffers[0] : g_buffers[std::min<uint32_t>(uint32_t(i) + 1, g_buffer_count - 1)];
    W32(s, R32(s) + 1);
    return s;
}

HLE_STDCALL(D3DDevice_GetRenderTarget2, 0) {
    uint32_t s = Dev(kDevRenderTarget);
    if (s) W32(s, R32(s) + 1);
    return s;
}

HLE_STDCALL(D3DDevice_GetDepthStencilSurface2, 0) {
    uint32_t s = Dev(kDevDepth);
    if (s) W32(s, R32(s) + 1);
    return s;
}

namespace {
std::atomic<uint32_t> g_presents{0};
}  // namespace

HLE_STDCALL(D3DDevice_Swap, 1) {
    app::SplashFirstFrame();
    // handed over from the previous instance: the window opens on the second frame, the first picture with
    // anything drawn over it right after
    if (app::HandingOver() && g_presents >= 2) {
        {
            std::lock_guard<std::recursive_mutex> lock(g_lock);
            if (g_backend) g_backend->Flush();
        }
        app::FinishHandover();
    }
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    g_presents++;
    // the kept display is shown until the title presents its own
    kernel::SetSavedDataAddress(0);
    // the ramp an inlined SetGammaRamp left for the vertical blank that shows this swap
    if (g_backend && XDK_CDevice_GammaPending != xdk::kUnknown && XDK_CDevice_GammaRamps != xdk::kUnknown) {
        uint32_t i = Dev(kDevSwapCount) & 1;
        if (Dev(XDK_CDevice_GammaPending + i * 4) == 1) {
            SetDev(XDK_CDevice_GammaPending + i * 4, 0);
            g_backend->SetGamma(static_cast<const uint8_t*>(HostPtr(kDevice + XDK_CDevice_GammaRamps + i * 0x300)));
        }
    }
    Walk(Dev(kDevPush));  // reports asked for at the end of the frame
    if (g_backend) g_backend->Present(SurfaceOf(g_buffers[1]));
    app::Pace();
    // like the XDK, the surfaces stay put and their memory rotates: titles keep back buffer pointers and copy its data
    uint32_t last = g_buffers[g_buffer_count - 1], data = R32(last + 4), lk = R32(last + 8);
    for (uint32_t i = g_buffer_count - 1; i > 0; i--) {
        W32(g_buffers[i] + 4, R32(g_buffers[i - 1] + 4));
        W32(g_buffers[i] + 8, R32(g_buffers[i - 1] + 8));
    }
    W32(g_buffers[0] + 4, data);
    W32(g_buffers[0] + 8, lk);
    g_state.rt = SurfaceOf(Dev(kDevRenderTarget));
    uint32_t n = Dev(kDevSwapCount) + 1;
    SetDev(kDevSwapCount, n);
    return n;
}

HLE_STDCALL(D3DDevice_Clear, 6) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    const uint8_t* rects = a[0] && a[1] ? static_cast<const uint8_t*>(HostPtr(a[1])) : nullptr;
    if (g_backend) g_backend->Clear(g_state, a[0], rects, a[2], a[3], ArgF(a, 4), a[5]);
    return 0;
}

HLE_STDCALL(D3DDevice_CopyRects, 5) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (g_backend) g_backend->Copy(SurfaceOf(a[0]), SurfaceOf(a[3]), a[2], a[1], a[4]);
    return 0;
}

HLE_STDCALL(D3DDevice_SetViewport, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t v = a[0];
    g_state.viewport = {R32(v), R32(v + 4), R32(v + 8), R32(v + 12), GF(v + 16), GF(v + 20)};
    g_state.scissor_count = 0;
    UpdateViewportConstants();
    return 0;
}

HLE_STDCALL(D3DDevice_SetScissors, 3) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    g_state.scissor_count = std::min<uint32_t>(a[0], 8);
    for (uint32_t i = 0; i < g_state.scissor_count * 4; i++) g_state.scissors[i] = R32(a[2] + i * 4);
    return 0;
}

HLE_STDCALL(D3DDevice_SetScreenSpaceOffset, 2) {
    g_state.ss_offset_x = ArgF(a, 0);
    g_state.ss_offset_y = ArgF(a, 1);
    UpdateViewportConstants();
    return 0;
}

// render states

HLE_FASTCALL(D3DDevice_SetRenderState_Simple, 0) {
    uint32_t method = a[0] & 0x1FFC, value = a[1];
    g_state.simple[method >> 2] = value;
    return 0;
}

HLE_STDCALL(D3DDevice_SetRenderStateNotInline, 2) {
    uint32_t rs = a[0], v = a[1];
    if (rs < gpu::kRsDeferredFirst) {
        uint32_t method = R32(kSimpleEncode + rs * 4) & 0x1FFC;
        g_state.simple[method >> 2] = v;
        W32(kRenderState + rs * 4, v);  // titles read their states back from the library's table
    } else if (rs < gpu::kRsMax) {
        SetComplex(rs, v);
    }
    return 0;
}

#define COMPLEX_STATE(name, rs)                       \
    HLE_STDCALL(D3DDevice_SetRenderState_##name, 1) { \
        SetComplex(rs, a[0]);                         \
        return 0;                                     \
    }
COMPLEX_STATE(PSTextureModes, gpu::kRsPsTextureModes)
COMPLEX_STATE(VertexBlend, gpu::kRsVertexBlend)
COMPLEX_STATE(FogColor, gpu::kRsFogColor)
COMPLEX_STATE(FillMode, gpu::kRsFillMode)
COMPLEX_STATE(BackFillMode, gpu::kRsBackFillMode)
COMPLEX_STATE(TwoSidedLighting, gpu::kRsTwoSidedLighting)
COMPLEX_STATE(NormalizeNormals, gpu::kRsNormalizeNormals)
COMPLEX_STATE(ZEnable, gpu::kRsZEnable)
COMPLEX_STATE(StencilEnable, gpu::kRsStencilEnable)
COMPLEX_STATE(StencilFail, gpu::kRsStencilFail)
COMPLEX_STATE(FrontFace, gpu::kRsFrontFace)
COMPLEX_STATE(CullMode, gpu::kRsCullMode)
COMPLEX_STATE(TextureFactor, gpu::kRsTextureFactor)
COMPLEX_STATE(LogicOp, gpu::kRsLogicOp)
COMPLEX_STATE(EdgeAntiAlias, gpu::kRsEdgeAntiAlias)
COMPLEX_STATE(MultiSampleAntiAlias, gpu::kRsMultiSampleAntiAlias)
COMPLEX_STATE(MultiSampleMask, gpu::kRsMultiSampleMask)
COMPLEX_STATE(MultiSampleMode, gpu::kRsMultiSampleMode)
COMPLEX_STATE(MultiSampleRenderTargetMode, gpu::kRsMultiSampleRenderTargetMode)
COMPLEX_STATE(ShadowFunc, gpu::kRsShadowFunc)
COMPLEX_STATE(LineWidth, gpu::kRsLineWidth)
COMPLEX_STATE(SampleAlpha, gpu::kRsSampleAlpha)
COMPLEX_STATE(Dxt1NoiseEnable, gpu::kRsDxt1NoiseEnable)
COMPLEX_STATE(YuvEnable, gpu::kRsYuvEnable)
COMPLEX_STATE(OcclusionCullEnable, gpu::kRsOcclusionCullEnable)
COMPLEX_STATE(StencilCullEnable, gpu::kRsStencilCullEnable)
COMPLEX_STATE(RopZCmpAlwaysRead, gpu::kRsRopZCmpAlwaysRead)
COMPLEX_STATE(RopZRead, gpu::kRsRopZRead)
COMPLEX_STATE(DoNotCullUncompressed, gpu::kRsDoNotCullUncompressed)

HLE_STDCALL(D3DDevice_SetTextureState_TexCoordIndex, 2) {
    g_state.tss[a[0] & 3][gpu::kTssTexCoordIndex] = a[1];
    W32(kTextureState + ((a[0] & 3) * kStageStates + gpu::kTssTexCoordIndex) * 4, a[1]);
    return 0;
}
HLE_STDCALL(D3DDevice_SetTextureState_BumpEnv, 3) {
    if (a[1] >= gpu::kTssBumpEnvMat00 && a[1] <= gpu::kTssBumpEnvLOffset) g_state.tss[a[0] & 3][a[1]] = a[2];
    return 0;
}
HLE_STDCALL(D3DDevice_SetTextureState_BorderColor, 2) {
    g_state.tss[a[0] & 3][gpu::kTssBorderColor] = a[1];
    return 0;
}
HLE_STDCALL(D3DDevice_SetTextureState_ColorKeyColor, 2) {
    g_state.tss[a[0] & 3][gpu::kTssColorKeyColor] = a[1];
    return 0;
}
HLE_STDCALL(D3DDevice_SetTextureStageStateNotInline, 3) {
    uint32_t stage = a[0] & 3, type = a[1], v = a[2];
    if (type < gpu::kTssCount) {
        g_state.tss[stage][type] = v;
        W32(kTextureState + (stage * kStageStates + type) * 4, v);
    }
    return 0;
}

// the XDK's transform types: view 0, projection 1, texture 2-5, world 6-9; the renderer keeps Direct3D 8's numbers
// (view 2, projection 3, texture 16, world 256). The device copy is what GetTransform reads.
HLE_STDCALL(D3DDevice_SetTransform, 2) {
    uint32_t t = a[0];
    if (t >= 10) return 0;
    std::memcpy(HostPtr(kDevice + kDevTransforms + t * 64), HostPtr(a[1]), 64);
    uint32_t pc = t < 2 ? t + 2 : t < 6 ? t + 14 : t + 250;
    std::memcpy(g_state.transforms[pc].data(), HostPtr(a[1]), 64);
    return 0;
}

HLE_STDCALL(D3DDevice_SetMaterial, 1) {
    std::memcpy(g_state.material.data(), HostPtr(a[0]), 17 * 4);
    return 0;
}

// the XDK keeps its own light lists, the renderer gets a copy
HLE_WRAP(D3DDevice_SetLight, 2) {
    uint32_t i = a[0], l = a[1];
    if (i < 8 && l) {
        std::lock_guard<std::recursive_mutex> lock(g_lock);
        std::memcpy(g_state.lights[i].data(), HostPtr(l), 26 * 4);
    }
}
HLE_WRAP(D3DDevice_LightEnable, 2) {
    uint32_t i = a[0], on = a[1];
    if (i < 8) {
        std::lock_guard<std::recursive_mutex> lock(g_lock);
        g_state.light_enable = on ? g_state.light_enable | 1u << i : g_state.light_enable & ~(1u << i);
    }
}

// resources in use

HLE_STDCALL(D3DDevice_SetTexture, 2) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t stage = a[0] & 3, tex = a[1];
    uint32_t old = Dev(kDevTextures + stage * 4);
    AddRef(ctx, tex);
    SetDev(kDevTextures + stage * 4, tex);
    Release(ctx, old);
    g_state.textures[stage] = tex;
    W32(kDirtyFlags, R32(kDirtyFlags) | XDK_DirtyFlags_Texture);
    return 0;
}

// SetPalette(stage, palette): the colours a P8 texture of the stage looks up
HLE_STDCALL(D3DDevice_SetPalette, 2) {
    XDK_REQUIRE("d3d", XDK_KEY(CDevice_Palettes));
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t stage = a[0] & 3, pal = a[1];
    uint32_t old = Dev(kDevPalettes + stage * 4);
    AddRef(ctx, pal);
    SetDev(kDevPalettes + stage * 4, pal);
    Release(ctx, old);
    g_state.palettes[stage] = pal;
    return 0;
}

// SwitchTexture(method, data, format), fastcall: a swizzled texture of the bound one's shape replaces it. It is
// bound through a header of the stage's own holding the bound texture's fields with the new data and format.
HLE_FASTCALL(D3DDevice_SwitchTexture, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    static uint32_t headers[4];
    // texture methods from 0x1B00, 0x40 per stage
    uint32_t stage = (((a[0] & 0x1FFC) - 0x1B00) >> 6) & 3, cur = g_state.textures[stage];
    uint32_t& h = headers[stage];
    if (!h) h = mem::AllocSystem(20);
    if (cur && cur != h) std::memcpy(HostPtr(h), HostPtr(cur), 20);
    W32(h + 4, a[1]);
    W32(h + 12, a[2]);
    g_state.textures[stage] = h;
    return 0;
}

HLE_STDCALL(D3DDevice_SetStreamSource, 3) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t n = a[0] & 15, vb = a[1], stride = a[2];
    uint32_t old = R32(kStreams + n * kStreamSize + XDK_Stream_Buffer);
    AddRef(ctx, vb);
    W32(kStreams + n * kStreamSize + XDK_Stream_Stride, stride);
    W32(kStreams + n * kStreamSize + XDK_Stream_Buffer, vb);
    Release(ctx, old);
    g_state.streams[n] = {vb, stride};
    return 0;
}

HLE_STDCALL(D3DDevice_SetIndices, 2) {
    XDK_REQUIRE("d3d", XDK_DATA(D3D_IndexData));
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t ib = a[0];
    uint32_t old = Dev(kDevIndexBuffer);
    AddRef(ctx, ib);
    W32(kIndexData, ib ? R32(ib + 4) : 0);
    SetDev(kDevIndexBuffer, ib);
    SetDev(kDevBaseVertex, a[1]);
    Release(ctx, old);
    g_state.index_buffer = ib;
    g_state.index_data = ib ? R32(ib + 4) : 0;
    g_state.base_vertex = a[1];
    return 0;
}

// shaders

HLE_STDCALL(D3DDevice_CreateVertexShader, 4) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t decl = a[0], func = a[1], out = a[2];
    auto vs = std::make_shared<gpu::VertexShader>();
    if (decl) {
        for (uint32_t i = 0; i < 1024; i++) {
            uint32_t t = R32(decl + i * 4);
            vs->decl.push_back(t);
            if (t == 0xFFFFFFFF) break;
        }
    }
    if (func) {
        uint32_t hdr = R32(func);
        vs->program_type = hdr & 0xFFFF;
        uint32_t n = hdr >> 16;
        for (uint32_t i = 0; i < n * 4; i++) vs->program.push_back(R32(func + 4 + i * 4));
    }
    gpu::ParseDeclaration(*vs);
    uint32_t rec = mem::AllocSystem(16, 16);
    vs->handle = rec | 1;
    g_vs[vs->handle] = vs;
    W32(out, vs->handle);
    LOG_FIRST(64, "d3d: vertex shader %08X, %zu decl tokens, %zu instructions, type %X", vs->handle, vs->decl.size(),
              vs->program.size() / 4, vs->program_type);
    return 0;
}

HLE_STDCALL(D3DDevice_DeleteVertexShader, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    auto it = g_vs.find(a[0]);
    if (it != g_vs.end()) {
        mem::FreeSystem(a[0] & ~1u);
        g_vs.erase(it);
    }
    return 0;
}

// (handle, data, size in and out): the program as it was given, the size alone without data or room
HLE_STDCALL(D3DDevice_GetVertexShaderFunction, 3) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    auto it = g_vs.find(a[0]);
    uint32_t words = it == g_vs.end() ? 0 : uint32_t(it->second->program.size());
    uint32_t need = words ? 4 + words * 4 : 0, room = R32(a[2]);
    W32(a[2], need);
    if (need > room) return a[1] ? 0x88760867 : 0;  // D3DERR_MOREDATA
    if (!a[1] || !need) return 0;
    W32(a[1], it->second->program_type | words / 4 << 16);
    for (uint32_t i = 0; i < words; i++) W32(a[1] + 4 + i * 4, it->second->program[i]);
    return 0;
}

HLE_STDCALL(D3DDevice_SetVertexShader, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t h = a[0];
    g_state.vs_handle = h;
    g_state.vs = nullptr;
    if (h & 1) {
        auto it = g_vs.find(h);
        if (it != g_vs.end()) {
            g_state.vs = it->second;
            // constants declared with the shader load when it is selected
            for (auto& [reg, v] : it->second->constants)
                if (reg < 192) g_state.vsc[reg] = v;
        } else {
            LOG_FIRST(8, "d3d: SetVertexShader with unknown handle %08X", h);
        }
    }
    return 0;
}

HLE_FASTCALL(D3DDevice_SetVertexShaderConstant1Fast, 0) {
    uint32_t reg = a[0];
    if (reg < 192) std::memcpy(g_state.vsc[reg].data(), HostPtr(a[1]), 16);
    return 0;
}

HLE_FASTCALL(D3DDevice_SetVertexShaderConstant1, 0) {
    uint32_t reg = a[0];
    if (reg < 192) std::memcpy(g_state.vsc[reg].data(), HostPtr(a[1]), 16);
    return 0;
}

HLE_FASTCALL(D3DDevice_SetVertexShaderConstant4, 0) {
    uint32_t reg = a[0], src = a[1];
    for (uint32_t i = 0; i < 4 && reg + i < 192; i++) std::memcpy(g_state.vsc[reg + i].data(), HostPtr(src + i * 16), 16);
    return 0;
}

HLE_FASTCALL(D3DDevice_SetVertexShaderConstantNotInlineFast, 1) {
    uint32_t reg = a[0], src = a[1], dwords = a[2];
    for (uint32_t i = 0; i < dwords / 4 && reg + i < 192; i++) std::memcpy(g_state.vsc[reg + i].data(), HostPtr(src + i * 16), 16);
    return 0;
}

// Vertex programs by address. A title may load microcode into the GPU's 136 program slots itself
// (LoadVertexShaderProgram, LoadVertexShader) and pick where a draw's program starts (SelectVertexShader); the
// attribute layout then comes from a guest shader object, or from SetVertexShaderInput's override with its own
// vertex buffers. The host keeps the slots and builds a VertexShader from them for the draws.
namespace {
constexpr uint32_t kProgramSlots = 136;
// guest vertex shader object (XDK 5849): flags +4, program dwords +0xC, VERTEXATTRIBUTEFORMAT +0x14
// (16 slots of stream, offset, D3DVSDT format, tessellation), its program as push buffer commands +0x114
constexpr uint32_t kVsoFlags = 0x4, kVsoProgramDwords = 0xC, kVsoAttributes = 0x14, kVsoProgram = 0x114;
std::array<uint32_t, kProgramSlots * 4> g_slots{};
uint32_t g_slot_start = 0;
uint32_t g_slot_object = 0;  // SelectVertexShader's object, 0 none
uint32_t g_input_object = 0;  // SetVertexShaderInput's override object, 0 none
struct StreamInput {
    uint32_t vb = 0, stride = 0, offset = 0;
};
std::array<StreamInput, 16> g_stream_inputs{};
uint32_t g_stream_input_count = 0;
std::unordered_map<uint64_t, std::shared_ptr<gpu::VertexShader>> g_slot_vs;

// push buffer commands of a program and its constants: 0x0B00 instructions at the load slot (set by 0x1E9C),
// 0x0B80 constants at the constant slot (set by 0x1EA4)
void RunProgramCommands(uint32_t p, uint32_t end, uint32_t slot) {
    uint32_t constant = 0;
    while (p + 4 <= end) {
        uint32_t head = R32(p), method = head & 0x1FFC, n = (head >> 18) & 0x7FF;
        p += 4;
        if ((head & 0xA0030003) || n * 4 > end - p) {
            LOG_FIRST(8, "d3d: vertex program commands stop at %08X", head);
            return;
        }
        for (uint32_t i = 0; i < n; i++, p += 4) {
            uint32_t m = head & 0x40000000 ? method : method + i * 4, v = R32(p);
            if (m >= 0x0B00 && m < 0x0B80) {
                if (slot < kProgramSlots * 4) g_slots[slot] = v;
                slot++;
            } else if (m == 0x1E9C) {
                slot = v * 4;
            } else if (m >= 0x0B80 && m < 0x0C00) {
                uint32_t r = constant / 4;
                if (r < 192) std::memcpy(&g_state.vsc[r][constant % 4], &v, 4);
                constant++;
            } else if (m == 0x1EA4) {
                constant = v * 4;  // hardware register, D3D register + 96
            }
        }
    }
}

// the VertexShader the next draws use: the slots from the start to the instruction with the final bit, the
// override's layout or the selected object's
void SelectFromSlots() {
    uint32_t object = g_input_object ? g_input_object : g_slot_object;
    if (!object) return;
    auto vs = std::make_shared<gpu::VertexShader>();
    vs->program_type = 0x2078;
    for (uint32_t i = g_slot_start; i < kProgramSlots; i++) {
        vs->program.insert(vs->program.end(), &g_slots[i * 4], &g_slots[i * 4] + 4);
        if (g_slots[i * 4 + 3] & 1) break;  // FINAL
    }
    uint32_t vaf = object + kVsoAttributes;
    for (uint32_t r = 0; r < 16; r++) {
        uint32_t stream = R32(vaf + r * 16), offset = R32(vaf + r * 16 + 4), format = R32(vaf + r * 16 + 8);
        if ((format & 0xF0) == 0 || stream >= 16) continue;  // D3DVSDT_NONE: no element
        if (g_input_object && stream < g_stream_input_count) offset += g_stream_inputs[stream].offset;
        vs->inputs[r] = {int(stream), offset, format & 0xFF};
    }
    // bytes per vertex: the override's strides, else the end of each stream's last element (inline vertices)
    for (uint32_t r = 0; r < 16; r++) {
        const auto& in = vs->inputs[r];
        if (in.stream < 0) continue;
        uint32_t t = in.type, n = (t >> 4) & 15, bytes;
        switch (t == 0x72 ? 7 : t & 15) {  // D3DVSDT kind; 0x72 FLOAT2H is three floats
            case 7: bytes = 12; break;
            case 0: case 6: bytes = 4; break;  // D3DCOLOR, NORMPACKED3
            case 1: case 5: bytes = 2 * n; break;  // NORMSHORT, SHORT
            case 4: bytes = n; break;  // PBYTE
            default: bytes = 4 * n; break;  // FLOAT
        }
        auto& st = vs->stream_stride[size_t(in.stream)];
        st = std::max(st, in.offset + bytes);
    }
    for (uint32_t i = 0; g_input_object && i < g_stream_input_count; i++) vs->stream_stride[i] = g_stream_inputs[i].stride;
    uint64_t h = gpu::Hash(vs->program.data(), vs->program.size() * 4);
    h = gpu::Hash(vs->inputs.data(), sizeof(vs->inputs), h);
    h = gpu::Hash(vs->stream_stride.data(), sizeof(vs->stream_stride), h);
    auto& cached = g_slot_vs[h];
    if (!cached) {
        vs->hash = h;
        vs->handle = object | 1;
        cached = vs;
        LOG_FIRST(32, "d3d: vertex program at slot %u, %zu instructions, object %08X%s", g_slot_start,
                  vs->program.size() / 4, object, g_input_object ? " (input override)" : "");
    }
    g_state.vs = cached;
    g_state.vs_handle = object | 1;
}
}  // namespace

// (function, slot): the header's high word counts the instructions that follow it
HLE_STDCALL(D3DDevice_LoadVertexShaderProgram, 2) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t func = a[0], slot = a[1], n = R32(func) >> 16;
    for (uint32_t i = 0; i < n * 4 && slot * 4 + i < kProgramSlots * 4; i++) g_slots[slot * 4 + i] = R32(func + 4 + i * 4);
    if (g_slot_object || g_input_object) SelectFromSlots();
    return 0;
}

// (handle, slot): the object's program, as the push buffer commands it keeps
HLE_STDCALL(D3DDevice_LoadVertexShader, 2) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t object = a[0] & ~1u, p = object + kVsoProgram;
    if (!object) return 0;
    RunProgramCommands(p, p + R32(object + kVsoProgramDwords) * 4, a[1] * 4);
    if (g_slot_object || g_input_object) SelectFromSlots();
    return 0;
}

// (handle, slot): 0 keeps the layout and moves the start
HLE_STDCALL(D3DDevice_SelectVertexShader, 2) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t h = a[0];
    if (h) {
        g_slot_object = h & ~1u;
        if (XDK_CDevice_VertexShader != xdk::kUnknown) SetDev(XDK_CDevice_VertexShader, g_slot_object);
        if (XDK_CDevice_VertexShaderHandle != xdk::kUnknown) SetDev(XDK_CDevice_VertexShaderHandle, h);
    }
    g_slot_start = std::min(a[1], kProgramSlots - 1);
    if (XDK_CDevice_VertexShaderStart != xdk::kUnknown) SetDev(XDK_CDevice_VertexShaderStart, a[1]);
    SelectFromSlots();
    return 0;
}

// (handle, count, STREAM_INPUT[count] of vertex buffer, stride, offset): the handle's layout over these buffers
// until a call with handle 0. The buffers are held while bound, as the library does.
HLE_STDCALL(D3DDevice_SetVertexShaderInput, 3) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t h = a[0], n = std::min<uint32_t>(a[1], 16), in = a[2];
    std::array<StreamInput, 16> next{};
    for (uint32_t i = 0; h && in && i < n; i++) next[i] = {R32(in + i * 12), R32(in + i * 12 + 4), R32(in + i * 12 + 8)};
    for (uint32_t i = 0; h && i < n; i++) AddRef(ctx, next[i].vb);
    for (uint32_t i = 0; i < g_stream_input_count; i++) Release(ctx, g_stream_inputs[i].vb);
    g_stream_inputs = next;
    g_stream_input_count = h ? n : 0;
    g_input_object = h & ~1u;
    for (uint32_t i = 0; i < g_stream_input_count; i++) g_state.streams[i] = {g_stream_inputs[i].vb, g_stream_inputs[i].stride};
    if (g_input_object || g_slot_object) SelectFromSlots();
    return 0;
}
HLE_STDCALL(D3DDevice_SetShaderConstantMode, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    g_own_constants = a[0] & 0x10;  // D3DSCM_NORESERVEDCONSTANTS
    UpdateViewportConstants();
    return 0;
}

HLE_STDCALL(D3DDevice_CreatePixelShader, 2) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    auto ps = std::make_shared<gpu::PixelShader>();
    std::memcpy(ps->def, HostPtr(a[0]), sizeof(ps->def));
    uint32_t rec = mem::AllocSystem(16, 16);
    ps->handle = rec;
    g_ps[rec] = ps;
    W32(a[1], rec);
    return 0;
}

HLE_STDCALL(D3DDevice_DeletePixelShader, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (g_ps.erase(a[0])) mem::FreeSystem(a[0]);
    return 0;
}

HLE_STDCALL(D3DDevice_SetPixelShader, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    g_state.ps = nullptr;
    if (!a[0]) return 0;
    auto it = g_ps.find(a[0]);
    if (it != g_ps.end()) {
        g_state.ps = it->second;
    } else if (XDK_PixelShader_Definition != xdk::kUnknown) {
        // made by library code the title inlined: the object points at its definition, handles are reused
        static std::unordered_map<uint32_t, std::shared_ptr<gpu::PixelShader>> made;
        const void* def = HostPtr(R32(a[0] + XDK_PixelShader_Definition));
        auto& ps = made[a[0]];
        if (!ps || std::memcmp(ps->def, def, sizeof(ps->def))) {
            ps = std::make_shared<gpu::PixelShader>();
            ps->handle = a[0];
            std::memcpy(ps->def, def, sizeof(ps->def));
        }
        g_state.ps = ps;
    } else {
        LOG_FIRST(8, "d3d: SetPixelShader with unknown handle %08X", a[0]);
        return 0;
    }
    // the definition loads the combiner state like the XDK pushes it
    const uint32_t* d = g_state.ps->def;
    auto put = [&](uint32_t method, uint32_t v) { g_state.simple[method >> 2] = v; };
    for (int i = 0; i < 8; i++) {
        put(gpu::kMPsAlphaInputs + i * 4, d[0 + i]);
        put(gpu::kMPsConstant0 + i * 4, d[10 + i]);
        put(gpu::kMPsConstant1 + i * 4, d[18 + i]);
        put(gpu::kMPsAlphaOutputs + i * 4, d[26 + i]);
        put(gpu::kMPsRgbInputs + i * 4, d[34 + i]);
        put(gpu::kMPsRgbOutputs + i * 4, d[45 + i]);
    }
    put(gpu::kMPsFinalAbcd, d[8]);
    put(gpu::kMPsFinalEfgh, d[9]);
    put(gpu::kMPsCompareMode, d[42]);
    put(gpu::kMPsFinalConstant0, d[43]);
    put(gpu::kMPsFinalConstant1, d[44]);
    put(gpu::kMPsCombinerCount, d[53]);
    put(gpu::kMPsDotMapping, d[55]);
    put(gpu::kMPsInputTexture, d[56]);
    SetComplex(gpu::kRsPsTextureModes, d[54]);
    return 0;
}

HLE_STDCALL(D3DDevice_SetPixelShaderConstant, 3) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t reg = a[0], src = a[1], n = a[2];
    for (uint32_t i = 0; i < n && reg + i < 16; i++) std::memcpy(g_state.psc[reg + i].data(), HostPtr(src + i * 16), 16);
    gpu::ApplyPixelConstants(g_state, reg, n);
    return 0;
}

// drawing

HLE_STDCALL(D3DDevice_DrawVertices, 3) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Walk(Dev(kDevPush));  // a visibility test begun since the last draw counts this one
    SyncDeferred();
    if (g_backend) g_backend->Draw(g_state, a[0], a[1], a[2], nullptr);
    return 0;
}

HLE_STDCALL(D3DDevice_DrawIndexedVertices, 3) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Walk(Dev(kDevPush));
    SyncDeferred();
    const uint8_t* indices = a[2] ? static_cast<const uint8_t*>(HostPtr(a[2])) : nullptr;
    if (g_backend) g_backend->Draw(g_state, a[0], 0, a[1], indices);
    return 0;
}

// DrawVerticesUP(prim, count, vertices, stride): the vertices are copied to a ring the render thread reads, stream 0
// points there through a vertex buffer header for the draw. A half of the ring is written again once the draws
// that read it are done.
namespace {
constexpr uint32_t kUpRing = 4u << 20, kUpHalf = kUpRing / 2;
uint32_t g_up_ring = 0, g_up_pos = 0, g_up_header = 0, g_up_fence[2] = {};

uint32_t UpVertices(const void* src, uint32_t bytes) {
    if (!g_up_ring) {
        g_up_ring = mem::AllocContiguous(kUpRing, 0, 0xFFFFFFFF, 0x1000);
        g_up_header = mem::AllocSystem(20);
        std::memset(HostPtr(g_up_header), 0, 20);
        W32(g_up_header, 1);  // a vertex buffer, one reference
    }
    bytes = std::min(bytes, kUpHalf);
    uint32_t room = (bytes + 63) & ~63u;
    uint32_t pos = g_up_pos + room > kUpRing ? 0 : g_up_pos;
    uint32_t from = g_up_pos ? (g_up_pos - 1) / kUpHalf : 1, to = (pos + room - 1) / kUpHalf;
    if (to != from) {
        g_up_fence[from] = g_backend->Fence();
        g_backend->WaitFence(g_up_fence[to]);
    }
    std::memcpy(HostPtr(g_up_ring + pos), src, bytes);
    g_up_pos = pos + room;
    W32(g_up_header + 4, mem::PhysicalFromVirtual(g_up_ring + pos));
    return g_up_header;
}

void DrawUp(uint32_t prim, uint32_t count, const void* vertices, uint32_t stride) {
    if (!g_backend || !count || !stride) return;
    SyncDeferred();
    auto stream = g_state.streams[0];
    g_state.streams[0] = {UpVertices(vertices, count * stride), stride};
    g_backend->Draw(g_state, prim, 0, count, nullptr);
    g_state.streams[0] = stream;
}

// bytes per vertex of the bound format: stream 0 of the FVF or of the shader's declaration
uint32_t VertexBytes() {
    if (g_state.vs_handle & 1) return g_state.vs ? g_state.vs->stream_stride[0] : 0;
    gpu::VertexShader fvf;
    gpu::FvfInputs(g_state.vs_handle, fvf);
    return fvf.stream_stride[0];
}

uint32_t g_push_begin = 0;
}  // namespace

// Begin/End immediate mode. Titles inline Begin and the vertex data calls, so their commands are read from the scratch buffer.
namespace d3d {
namespace {
uint32_t g_im_prim = 0, g_im_mask = 0;  // open primitive, slots it wrote
float g_im_attr[64];                    // 16 slots of x y z w
std::vector<float> g_im_vertices;       // every slot per vertex

// visibility tests: depth passing pixels are counted while enabled, a report gets the count since the clear,
// at a physical address in the first 16 MB
void ApplyVisibilityMethod(uint32_t m, uint32_t v) {
    constexpr uint32_t kReportAddressMask = 0xFFFFFF;
    if (m == gpu::kMCountEnable) g_state.simple[gpu::kMCountEnable >> 2] = v;
    if (!g_backend) return;
    if (m == gpu::kMClearReport) g_backend->ClearReport();
    if (m == gpu::kMGetReport) g_backend->GetReport(mem::VirtualFromPhysical(v & kReportAddressMask));
}

// draws the open primitive: the slots it wrote are FLOAT4 inputs of the bound program, or of the fixed pipeline
void EndPrimitive() {
    struct Made {
        std::shared_ptr<gpu::VertexShader> from, vs;
    };
    // the render thread tells shaders apart by pointer: one object per program and slot set, the program kept alive
    static std::map<std::pair<gpu::VertexShader*, uint32_t>, Made> made;
    static std::vector<float> packed;
    uint32_t prim = g_im_prim, count = uint32_t(g_im_vertices.size() / 64), handle = g_state.vs_handle;
    g_im_prim = 0;
    if (!prim || !count) return;
    if (!(handle & 1) && (handle & 0xE) == 4) LOG_FIRST(1, "d3d: pretransformed immediate mode vertices are not supported");
    auto bound = g_state.vs, from = handle & 1 ? bound : nullptr;
    Made& m = made[{from.get(), g_im_mask}];
    if (!m.vs) {
        m.from = from;
        m.vs = std::make_shared<gpu::VertexShader>();
        if (from) {
            m.vs->program = from->program;
            m.vs->program_type = from->program_type;
        }
        for (uint32_t s = 0; s < 16; s++)
            if (g_im_mask >> s & 1) {
                m.vs->inputs[s] = {0, m.vs->stream_stride[0], 0x42};
                m.vs->stream_stride[0] += 16;
            }
    }
    packed.clear();
    for (uint32_t v = 0; v < count; v++)
        for (uint32_t s = 0; s < 16; s++)
            if (g_im_mask >> s & 1) packed.insert(packed.end(), &g_im_vertices[v * 64 + s * 4], &g_im_vertices[v * 64 + s * 4] + 4);
    g_state.vs = m.vs;
    g_state.vs_handle = handle | 1;
    DrawUp(prim, count, packed.data(), m.vs->stream_stride[0]);
    g_state.vs = bound;
    g_state.vs_handle = handle;
}

// reads the commands pushed since the last call: vertex data between a begin and an end is drawn, visibility tests
// are run, the rest is skipped
void Walk(uint32_t end) {
    if (!g_walk || end < g_walk || end > g_scratch_push + g_scratch_size) return;
    for (uint32_t p = g_walk; p + 4 <= end;) {
        // count in bits 18 to 28, bit 30 keeps the method for every value
        uint32_t head = R32(p), method = head & 0x1FFC, n = (head >> 18) & 0x7FF;
        p += 4;
        if ((head & 0xA0030003) || n * 4 > end - p) break;  // anything but a method header
        for (uint32_t i = 0; i < n; i++, p += 4) {
            uint32_t m = head & 0x40000000 ? method : method + i * 4, v = R32(p);
            float f, *a = g_im_attr;
            bool last = true;  // the slot's last component
            std::memcpy(&f, &v, 4);
            ApplyVisibilityMethod(m, v);
#ifdef XBE_WALK_STATS
            {  // the methods titles push themselves, logged now and then
                static std::map<uint32_t, uint64_t> seen;
                static uint64_t total = 0;
                seen[m]++;
                if (++total % 20000 == 0 || total == 2000) {
                    std::string list;
                    for (auto& [k, c] : seen) list += " " + std::to_string(k) + "x" + std::to_string(c);
                    LOGI("d3d: walked methods (decimal method x count):%s", list.c_str());
                }
            }
#endif
            if (m == 0x17FC) {  // begin/end, 0 ends
                if (v) {
                    g_im_prim = v;
                    g_im_mask = 0;
                    g_im_vertices.clear();
                } else {
                    EndPrimitive();
                }
                continue;
            } else if (m >= 0x1880 && m < 0x1900) {  // 2 floats, z = 0 and w = 1
                a += (m - 0x1880) / 8 * 4;
                last = (m - 0x1880) / 4 & 1;
                a[last] = f;
                a[2] = 0;
                a[3] = 1;
            } else if (m >= 0x1940 && m < 0x1980) {  // 4 bytes
                a += (m - 0x1940) / 4 * 4;
                for (int c = 0; c < 4; c++) a[c] = (v >> c * 8 & 0xFF) / 255.0f;
            } else if (m >= 0x1A00 && m < 0x1B00) {  // 4 floats
                a += (m - 0x1A00) / 16 * 4;
                last = ((m - 0x1A00) / 4 & 3) == 3;
                a[(m - 0x1A00) / 4 & 3] = f;
            } else if (m >= 0x1500 && m < 0x150C) {  // position, 3 floats and w = 1
                last = m == 0x1508;
                a[(m - 0x1500) / 4] = f;
                a[3] = 1;
            } else if (m >= 0x1518 && m < 0x1528) {  // position, 4 floats
                last = m == 0x1524;
                a[(m - 0x1518) / 4] = f;
            } else {
                continue;
            }
            if (!g_im_prim) continue;
            g_im_mask |= 1u << (a - g_im_attr) / 4;
            if (a == g_im_attr && last) g_im_vertices.insert(g_im_vertices.end(), g_im_attr, g_im_attr + 64);
        }
    }
    g_walk = end;
}
}  // namespace
}  // namespace d3d

HLE_STDCALL(D3DDevice_DrawVerticesUP, 4) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Walk(Dev(kDevPush));
    DrawUp(a[0], a[1], HostPtr(a[2]), a[3]);
    return 0;
}

// BeginPush(dwords) hands out room for raw hardware commands, EndPush(end) runs what the title wrote there:
// a primitive between two begin/end writes with its vertices inline, any other method as a simple state.
HLE_STDCALL(D3DDevice_BeginPush, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    g_push_begin = ScratchPush(a[0] * 4);
    return g_push_begin;
}

// the constant and program load slots raw pushes carry on from, as the hardware's registers do
static uint32_t g_push_constant = 0, g_push_slot = 0;

HLE_STDCALL(D3DDevice_EndPush, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    constexpr uint32_t kBeginEnd = 0x17FC, kInlineArray = 0x1818;
    static std::vector<uint32_t> vertices;
    uint32_t prim = 0;
    for (uint32_t p = g_push_begin, end = a[0]; g_push_begin && p + 4 <= end;) {
        // count in bits 18 to 28, bit 30 keeps the method for every value
        uint32_t head = R32(p), method = head & 0x1FFC, n = (head >> 18) & 0x7FF;
        p += 4;
        if ((head & 0xA0030003) || n * 4 > end - p) {  // anything but a method header
            LOG_FIRST(8, "d3d: EndPush stops at command %08X, %u bytes before the end", head, end - p);
            break;
        }
        for (uint32_t i = 0; i < n; i++, p += 4) {
            uint32_t m = head & 0x40000000 ? method : method + i * 4, v = R32(p);
            ApplyVisibilityMethod(m, v);
            if (m == kInlineArray) {
                if (prim) vertices.push_back(v);
            } else if (m >= 0x0B80 && m < 0x0C00) {  // TRANSFORM_CONSTANT at the constant slot (0x1EA4)
                uint32_t r = g_push_constant / 4;
                if (r < 192) std::memcpy(&g_state.vsc[r][g_push_constant % 4], &v, 4);
                g_push_constant++;
            } else if (m == 0x1EA4) {
                g_push_constant = v * 4;
            } else if (m >= 0x0B00 && m < 0x0B80) {  // TRANSFORM_PROGRAM at the load slot (0x1E9C)
                if (g_push_slot < kProgramSlots * 4) g_slots[g_push_slot] = v;
                g_push_slot++;
            } else if (m == 0x1E9C) {
                g_push_slot = v * 4;
            } else if (m == 0x1EA0) {  // TRANSFORM_PROGRAM_START
                g_slot_start = std::min(v, kProgramSlots - 1);
                if (g_slot_object || g_input_object) SelectFromSlots();
            } else if (m != kBeginEnd) {
                if (m >> 2 < g_state.simple.size()) g_state.simple[m >> 2] = v;
            } else if (v) {
                prim = v;
                vertices.clear();
            } else if (prim) {
                uint32_t stride = VertexBytes(), bytes = uint32_t(vertices.size()) * 4;
                if (stride && bytes % stride == 0) DrawUp(prim, bytes / stride, vertices.data(), stride);
                else LOG_FIRST(8, "d3d: EndPush primitive %u dropped, %u bytes of vertices of %u", prim, bytes, stride);
                prim = 0;
            }
        }
    }
    g_push_begin = 0;
    ScratchPush();
    return a[0];
}

// RunPushBuffer(push buffer, fixup): a recorded command stream, its fixups patched into a copy first. The arrays
// it sets (VERTEX_DATA_ARRAY offset and format per register, the format's low byte is the D3DVSDT type) are drawn
// through stand in vertex buffers, by index (ARRAY_ELEMENT16/32) or range (DRAW_ARRAYS); program loads, program
// start and transform constants go to the slots; texture offset and format to a header per stage; the rest as simple
// states. The title's own layout, streams and textures come back afterwards, as the library marks them dirty.
namespace {
struct PushArrays {
    uint32_t offset[16] = {}, format[16] = {};
};
uint32_t g_pb_vb[16] = {};       // stand in D3DVertexBuffer headers, Data = the array's address
uint32_t g_pb_tex[4] = {};       // stand in texture headers per stage
uint32_t g_pb_pal[4] = {};       // stand in palette headers per stage
std::map<uint32_t, uint32_t> g_pb_methods;  // method -> count, for the log of what is not handled

// the vertex arrays the library programmed for the current layout and streams: per register the element's physical
// address and its format (D3DVSDT type in the low byte, the stride from bit 8), as VERTEX_DATA_ARRAY sets them
PushArrays CurrentArrays() {
    PushArrays arr;
    gpu::VertexShader fvf;
    const gpu::VertexShader* layout = g_state.vs.get();
    if (!(g_state.vs_handle & 1)) {
        gpu::FvfInputs(g_state.vs_handle, fvf);
        layout = &fvf;
    }
    if (!layout) return arr;
    for (uint32_t r = 0; r < 16; r++) {
        const auto& in = layout->inputs[r];
        if (in.stream < 0 || in.stream >= 16 || !(in.type & 0xF0)) continue;
        const auto& st = g_state.streams[size_t(in.stream)];
        if (!st.vb) continue;
        uint32_t stride = st.stride ? st.stride : layout->stream_stride[size_t(in.stream)];
        arr.offset[r] = (R32(st.vb + 4) & 0x7FFFFFFF) + in.offset;
        arr.format[r] = (in.type & 0xFF) | stride << 8;
    }
    return arr;
}

void PushDraw(uint32_t prim, const PushArrays& arr, bool program, const std::vector<uint32_t>& idx,
              const std::vector<uint32_t>& inl) {
    if (!g_backend || !prim) return;
    auto vs = std::make_shared<gpu::VertexShader>();
    if (program && g_state.vs) vs->program = g_state.vs->program;
    vs->program_type = 0x2078;
    uint32_t inline_stride = 0;
    for (uint32_t r = 0; r < 16; r++) {
        uint32_t f = arr.format[r], size = (f >> 4) & 15, stride = f >> 8;
        if (!size) continue;
        vs->inputs[r] = {int(r), 0, f & 0xFF};
        vs->stream_stride[r] = stride;
        inline_stride += (f & 15) == 0 || (f & 15) == 6 ? 4 : (f & 15) == 1 || (f & 15) == 5 ? 2 * size
                       : (f & 15) == 4 ? size : 4 * size;
    }
    vs->hash = gpu::Hash(vs->program.data(), vs->program.size() * 4, gpu::Hash(vs->inputs.data(), sizeof(vs->inputs)));
    auto saved_vs = g_state.vs;
    uint32_t saved_handle = g_state.vs_handle;
    auto saved_streams = g_state.streams;
    g_state.vs = vs;
    g_state.vs_handle = 1;
    if (!inl.empty()) {  // INLINE_ARRAY: attributes packed in register order
        if (inline_stride && inl.size() * 4 % inline_stride == 0) {
            uint32_t off = 0;
            for (uint32_t r = 0; r < 16; r++) {
                if (vs->inputs[r].stream < 0) continue;
                vs->inputs[r] = {0, off, vs->inputs[r].type};
                uint32_t t = vs->inputs[r].type & 15, n = (vs->inputs[r].type >> 4) & 15;
                off += t == 0 || t == 6 ? 4 : t == 1 || t == 5 ? 2 * n : t == 4 ? n : 4 * n;
            }
            vs->stream_stride.fill(0);
            vs->stream_stride[0] = inline_stride;
            DrawUp(prim, uint32_t(inl.size() * 4 / inline_stride), inl.data(), inline_stride);
        } else {
            LOG_FIRST(8, "d3d: push buffer inline vertices dropped, %zu bytes of vertices of %u", inl.size() * 4, inline_stride);
        }
    } else if (!idx.empty()) {
        for (uint32_t r = 0; r < 16; r++) {
            if (vs->inputs[r].stream < 0) continue;
            if (!g_pb_vb[r]) g_pb_vb[r] = mem::AllocSystem(32, 16);
            W32(g_pb_vb[r], 0x00000001);  // one reference, D3DCOMMON_TYPE_VERTEXBUFFER (0)
            W32(g_pb_vb[r] + 4, arr.offset[r] & 0x7FFFFFFF);  // context DMA bit off: a physical address
            g_state.streams[r] = {g_pb_vb[r], vs->stream_stride[r]};
        }
        uint32_t top = *std::max_element(idx.begin(), idx.end());
        if (top < 0x10000) {
            static std::vector<uint16_t> i16;
            i16.assign(idx.begin(), idx.end());
            g_backend->Draw(g_state, prim, 0, uint32_t(i16.size()), reinterpret_cast<const uint8_t*>(i16.data()));
        } else {
            LOG_FIRST(8, "d3d: push buffer draw with index %u over 16 bits dropped", top);
        }
    }
    g_state.vs = saved_vs;
    g_state.vs_handle = saved_handle;
    g_state.streams = saved_streams;
}
}  // namespace

HLE_STDCALL(D3DDevice_RunPushBuffer, 2) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Walk(Dev(kDevPush));
    SyncDeferred();
    uint32_t pb = a[0], fx = a[1];
    uint32_t data = mem::VirtualFromPhysical(R32(pb + 4) & 0x0FFFFFFF), size = R32(pb + 0xC);
    static std::vector<uint32_t> buf;
    buf.assign(size / 4, 0);
    if (size) std::memcpy(buf.data(), HostPtr(data), buf.size() * 4);
    if (fx) {  // {bytes, offset, data[bytes]} ... 0xFFFFFFFF from the fixup's Data + Run
        uint32_t p = mem::VirtualFromPhysical(R32(fx + 4) & 0x0FFFFFFF) + R32(fx + 0xC);
        for (int guard = 0; guard < 65536; guard++) {
            uint32_t n = R32(p), off = R32(p + 4);
            if (n == 0xFFFFFFFF) break;
            if (n > size || off > size - n || (n | off) & 3) {
                LOG_FIRST(8, "d3d: push buffer fixup %u bytes at %u does not fit %u", n, off, size);
                break;
            }
            std::memcpy(reinterpret_cast<uint8_t*>(buf.data()) + off, HostPtr(p + 8), n);
            p += 8 + n;
        }
    }
    // the arrays start as the hardware has them: a push buffer may only draw, over the layout and buffers the
    // title set through the library before running it
    PushArrays arr = CurrentArrays();
    std::vector<uint32_t> idx, inl;
    uint32_t prim = 0, program_mode = g_state.vs && !g_state.vs->program.empty() ? 2 : 0, slot = 0, constant = 0;
    bool tex_dirty[4] = {};
    uint32_t tex_regs[4][8] = {};
    auto saved_textures = g_state.textures;
    auto saved_palettes = g_state.palettes;
    auto flush_textures = [&] {
        for (int t = 0; t < 4; t++) {
            if (!tex_dirty[t]) continue;
            if (!g_pb_tex[t]) g_pb_tex[t] = mem::AllocSystem(32, 16);
            uint32_t h = g_pb_tex[t], cur = g_state.textures[t];
            if (cur && cur != h) std::memcpy(HostPtr(h), HostPtr(cur), 20);
            else if (!cur) W32(h, 0x00040001);  // one reference, D3DCOMMON_TYPE_TEXTURE
            W32(h + 4, tex_regs[t][0]);   // offset
            W32(h + 12, tex_regs[t][1]);  // format
            uint32_t rect = tex_regs[t][7], pitch = tex_regs[t][4] >> 16;
            if (!(tex_regs[t][1] & 0xFFF00000) && rect)  // linear: D3D size from the image rect and pitch
                W32(h + 16, ((rect >> 16) - 1) | ((rect & 0xFFFF) - 1) << 12 | (std::max(pitch, 64u) / 64 - 1) << 24);
            g_state.textures[t] = h;
            tex_dirty[t] = false;
        }
    };
    size_t n = buf.size();
    for (size_t p = 0; p < n;) {
        uint32_t head = buf[p++];
        if (!head) continue;  // NOP
        if ((head & 0xE0000003) == 0x20000000 || (head & 3) == 1 || (head & 3) == 2 || head == 0x00020000) {
            LOG_FIRST(8, "d3d: push buffer %08X: jump, call or return %08X at dword %zu of %zu, stopped", pb, head, p - 1, n);
            break;
        }
        uint32_t method = head & 0x1FFC, count = (head >> 18) & 0x7FF;
        if ((head & 0xA0030003) || count > n - p) {
            LOG_FIRST(8, "d3d: push buffer %08X stops at command %08X", pb, head);
            break;
        }
        for (uint32_t i = 0; i < count; i++) {
            uint32_t m = head & 0x40000000 ? method : method + i * 4, v = buf[p++];
            ApplyVisibilityMethod(m, v);
            if (m >= 0x1720 && m < 0x1760) {
                arr.offset[(m - 0x1720) / 4] = v;
            } else if (m >= 0x1760 && m < 0x17A0) {
                arr.format[(m - 0x1760) / 4] = v;
            } else if (m == 0x17FC) {  // BEGIN_END
                if (v) {
                    prim = v;
                    idx.clear();
                    inl.clear();
                } else {
                    flush_textures();
                    PushDraw(prim, arr, program_mode == 2, idx, inl);
                    prim = 0;
                }
            } else if (m == 0x1800) {  // ARRAY_ELEMENT16, two per dword
                idx.push_back(v & 0xFFFF);
                idx.push_back(v >> 16);
            } else if (m == 0x1808) {  // ARRAY_ELEMENT32
                idx.push_back(v);
            } else if (m == 0x1810) {  // DRAW_ARRAYS: start, count - 1 in the top byte
                for (uint32_t k = 0, s0 = v & 0xFFFFFF; k <= v >> 24; k++) idx.push_back(s0 + k);
            } else if (m == 0x1818) {  // INLINE_ARRAY
                inl.push_back(v);
            } else if (m >= 0x0B00 && m < 0x0B80) {  // TRANSFORM_PROGRAM
                if (slot < kProgramSlots * 4) g_slots[slot] = v;
                slot++;
            } else if (m == 0x1E9C) {
                slot = v * 4;
            } else if (m == 0x1EA0) {  // TRANSFORM_PROGRAM_START
                g_slot_start = std::min(v, kProgramSlots - 1);
                if (g_slot_object || g_input_object) SelectFromSlots();
            } else if (m >= 0x0B80 && m < 0x0C00) {  // TRANSFORM_CONSTANT
                uint32_t r = constant / 4;
                if (r < 192) std::memcpy(&g_state.vsc[r][constant % 4], &v, 4);
                constant++;
            } else if (m == 0x1EA4) {
                constant = v * 4;
            } else if (m == 0x1E94) {  // TRANSFORM_EXECUTION_MODE: 0 fixed function, 2 program
                program_mode = v & 3;
                if (m >> 2 < g_state.simple.size()) g_state.simple[m >> 2] = v;
            } else if (m >= 0x1B00 && m < 0x1C00) {  // texture stage registers, 0x40 apart
                uint32_t t = (m - 0x1B00) >> 6, r = ((m - 0x1B00) & 0x3F) >> 2;
                if (r < 8) {
                    tex_regs[t][r] = v;
                    if (r <= 1) tex_dirty[t] = true;
                } else if (r == 8) {  // PALETTE: context DMA bit 0, length code bits 2 and 3, offset from bit 6
                    if (!g_pb_pal[t]) g_pb_pal[t] = mem::AllocSystem(16, 16);
                    W32(g_pb_pal[t], 0x00030001 | (v >> 2 & 3) << 30);  // one reference, D3DCOMMON_TYPE_PALETTE, size code
                    W32(g_pb_pal[t] + 4, v & 0x0FFFFFC0);               // Data: the colours' physical address
                    g_state.palettes[t] = g_pb_pal[t];
                }
                if (m >> 2 < g_state.simple.size()) g_state.simple[m >> 2] = v;
            } else {
                if (m >> 2 < g_state.simple.size()) g_state.simple[m >> 2] = v;
                g_pb_methods[m]++;
            }
        }
    }
    g_state.textures = saved_textures;
    g_state.palettes = saved_palettes;
    static uint32_t runs = 0;
    if (++runs == 200) {
        std::string list;
        for (auto& [m, c] : g_pb_methods) list += " " + std::to_string(m) + "x" + std::to_string(c);
        LOGI("d3d: push buffers, other methods (decimal method x count):%s", list.c_str());
    }
    return 0;
}

// frame buffer compression tiles are the console's memory controller's business
HLE_STDCALL(D3DDevice_SetTile, 2) { return 0; }

// the library's Begin pushes the start of the primitive, here it is opened
HLE_STDCALL(D3DDevice_Begin, 1) {
    XDK_REQUIRE("d3d", XDK_KEY(CDevice_Flags));
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Walk(Dev(kDevPush));
    g_im_prim = a[0];
    g_im_mask = 0;
    g_im_vertices.clear();
    SetDev(XDK_CDevice_Flags, Dev(XDK_CDevice_Flags) | 0x800);
    return 0;
}
// the library's End pushes the end of the primitive, here it is drawn
HLE_STDCALL(D3DDevice_End, 0) {
    XDK_REQUIRE("d3d", XDK_KEY(CDevice_Flags));
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Walk(Dev(kDevPush));
    EndPrimitive();
    SetDev(XDK_CDevice_Flags, Dev(XDK_CDevice_Flags) & ~0x1800u);
    return 0;
}
