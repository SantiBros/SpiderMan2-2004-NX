// The game's own 16:9 picture, wider rather than cut. The engine knows 16:9: its camera takes an aspect (4:3, or
// 16:9 for its 16:9 display modes, which the retail game never lists) and builds the projection from a horizontal
// field of view, so 16:9 there shows the same width as 4:3 and less height. With the port's widescreen picture
// (app::Widescreen()) the camera's aspect is 16:9 and its field of view the one that shows 4:3's height, more of the
// city at the sides (the game's own culling widens with it, its cone is the same field of view). The 640x480 frame is
// then shown 16:9 and what the game draws flat (HUD, menus, text) kept 4:3 in the middle (gl/gl_backend.cpp).
// Changed in play: the projection is made again at the next frame.
#include <cmath>
#include <cstring>

#include "app/app.h"
#include "core/log.h"
#include "cpu/x86_ops.h"

X86_EXTERN_FUNC(__imp__sub_00343930);
X86_EXTERN_FUNC(__imp__sub_003438E0);
X86_EXTERN_FUNC(__imp__sub_00343FF0);

namespace {

// the global view state: a pointer at 0x0050BDF0; +0x14C perspective (else orthographic), +0x208 the projection to
// be made again, +0x20C aspect, +0x210 horizontal field of view in degrees
constexpr uint32_t kView = 0x50BDF0, kPerspective = 0x14C, kDirty = 0x208, kAspect = 0x20C, kFov = 0x210;
constexpr float kAspect43 = 4.0f / 3.0f, kAspect169 = 16.0f / 9.0f;

// the field of view the game set, and the one written in its place
float g_set = 0, g_wide = -1;

float GetF(uint8_t* base, uint32_t va) {
    float v;
    std::memcpy(&v, &X86_MEM32(va), 4);
    return v;
}
void SetF(uint8_t* base, uint32_t va, float v) { std::memcpy(&X86_MEM32(va), &v, 4); }

// 4:3's height at 16:9: tan(fov / 2) four thirds as wide
float Wider(float fov) {
    return float(2.0 * std::atan(std::tan(double(fov) * M_PI / 360.0) * double(kAspect169 / kAspect43)) * 180.0 / M_PI);
}

void Apply(uint8_t* base, uint32_t view) {
    bool wide = app::Widescreen();
    bool dirty = false;
    // only the two aspects the game uses are changed
    float aspect = GetF(base, view + kAspect), want = wide ? kAspect169 : kAspect43;
    if ((std::fabs(aspect - kAspect43) < 1e-4f || std::fabs(aspect - kAspect169) < 1e-4f) && aspect != want) {
        SetF(base, view + kAspect, want);
        dirty = true;
        // the game sets its 4:3 again every frame: only the first few times in the log (each line is a write to the
        // SD card, which in 0.2.0 made the 16:9 picture slow)
        LOG_FIRST(4, "sm2: camera aspect %s", wide ? "16:9 (widescreen picture)" : "4:3");
    }
    if (X86_MEM32(view + kPerspective)) {
        float fov = GetF(base, view + kFov);
        if (wide && fov != g_wide && fov > 1.0f && fov < 150.0f) {
            g_set = fov;
            g_wide = Wider(fov);
            SetF(base, view + kFov, g_wide);
            dirty = true;
            LOG_FIRST(8, "sm2: field of view %.2f widened to %.2f", g_set, g_wide);
        } else if (!wide && fov == g_wide) {
            SetF(base, view + kFov, g_set);
            g_wide = -1;
            dirty = true;
        }
    }
    if (dirty) X86_MEM8(view + kDirty) = 1;
}

}  // namespace

// the projection made from the view state, when it changed (bool argument: only mark it changed)
X86_FUNC(sub_00343930) {
    if (uint32_t view = X86_MEM32(kView)) Apply(base, view);
    __imp__sub_00343930(ctx, base);
}

// GetPerspective(float* fov, float* near, float* far): the field of view the game set, not the widened one, so one
// read and set again is not widened twice
X86_FUNC(sub_003438E0) {
    uint32_t fov = X86_MEM32(ctx.esp + 4);
    __imp__sub_003438E0(ctx, base);
    if (fov && g_wide > 0 && GetF(base, fov) == g_wide) SetF(base, fov, g_set);
}

// WorldToScreen(float out[4], const float in[3]): a point of the world on the 640x480 screen, through the camera's
// projection. What is placed with it is drawn flat (the city map's icons and lines, markers over the world) and so
// narrowed to 4:3 in the middle (gl/gl_backend.cpp), while the point it marks is in the 16:9 picture: narrowed twice,
// the icons slid towards the middle and moved more slowly than the map under them. The x is widened here by what the
// picture narrows, so it lands back on its point.
X86_FUNC(sub_00343FF0) {
    uint32_t out = X86_MEM32(ctx.esp + 4);
    __imp__sub_00343FF0(ctx, base);
    uint32_t view = X86_MEM32(kView);
    if (!out || !view || !app::Widescreen() || !X86_MEM32(view + kPerspective)) return;
    if (std::fabs(GetF(base, view + kAspect) - kAspect169) > 1e-4f) return;
    // the screen's width as the game takes it (640, or its render target's): only the 640 wide frame is narrowed
    uint32_t target = X86_MEM32(view + 0x128), flags = X86_MEM32(0x50C2B8);
    uint32_t width = flags && X86_MEM8(flags + 0x34) && target && (X86_MEM8(target + 0x1C) & 8) ? 640 : target ? X86_MEM32(target + 8) : 0;
    if (width != 640) return;
    float x = GetF(base, out);
    SetF(base, out, 320.0f + (x - 320.0f) * (kAspect169 / kAspect43));
}
