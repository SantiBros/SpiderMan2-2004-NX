// The game's own 16:9 picture, wider rather than cut. The engine knows 16:9: its camera takes an aspect (4:3, or
// 16:9 for its 16:9 display modes, which the retail game never lists) and builds the projection from a horizontal
// field of view, so 16:9 there shows the same width as 4:3 and less height. With the port's widescreen picture
// (app::Widescreen()) the camera's aspect is 16:9 and its field of view the one that shows 4:3's height, more of the
// city at the sides (the game's own culling widens with it, its cone is the same field of view). The 640x480 frame is
// then shown 16:9 and what the game draws flat (HUD, menus, text) kept 4:3 in the middle (gl/gl_backend.cpp).
// The close view (app::WideClose()) is the engine's own 16:9 instead: the field of view the game set across the
// width, so what it shows is a third bigger and a little less is seen above and below; what it draws flat is drawn a
// tenth bigger (app::kCloseHud), and what is placed over the picture follows.
// Changed in play: the projection is made again at the next frame.
#include <cmath>
#include <cstring>

#include "app/app.h"
#include "core/log.h"
#include "cpu/x86_ops.h"

X86_EXTERN_FUNC(__imp__sub_00343930);
X86_EXTERN_FUNC(__imp__sub_003438E0);
X86_EXTERN_FUNC(__imp__sub_00343FF0);
X86_EXTERN_FUNC(__imp__sub_003443A0);

namespace {

// the global view state: a pointer at 0x0050BDF0; +0x14C perspective (else orthographic), +0x208 the projection to
// be made again, +0x20C aspect, +0x210 horizontal field of view in degrees
constexpr uint32_t kView = 0x50BDF0, kPerspective = 0x14C, kDirty = 0x208, kAspect = 0x20C, kFov = 0x210;
constexpr float kAspect43 = 4.0f / 3.0f, kAspect169 = 16.0f / 9.0f;

// the field of view the game set, and the one written in its place
float g_set = 0, g_wide = -1;

// the view's rectangle on its target (set by 0x00343D50), in the target's pixels, last row and column included
constexpr uint32_t kLeft = 0x1C4, kTop = 0x1C8, kRight = 0x1CC, kBottom = 0x1D0;
constexpr float kNarrowed = 0.75f;  // what the 16:9 picture keeps of the width of what the game draws flat
constexpr float kFrameW = 640, kFrameH = 480;

// how much bigger the game's flat drawing is shown: the close view's HUD
float HudSize() { return app::WideClose() ? app::kCloseHud : 1.0f; }

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

bool InPart(uint8_t* base, uint32_t view);

void Apply(uint8_t* base, uint32_t view) {
    bool wide = app::Widescreen();
    // the close view keeps the field of view the game set (its own 16:9), but for a view in part of the frame (the
    // minimap): it goes with the HUD, kept 4:3, so as in the wide view
    bool widen = wide && (!app::WideClose() || InPart(base, view));
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
        if (widen && fov != g_wide && fov > 1.0f && fov < 150.0f) {
            g_set = fov;
            g_wide = Wider(fov);
            SetF(base, view + kFov, g_wide);
            dirty = true;
            LOG_FIRST(8, "sm2: field of view %.2f widened to %.2f", g_set, g_wide);
        } else if (!widen && fov == g_wide) {
            SetF(base, view + kFov, g_set);
            g_wide = -1;
            dirty = true;
        }
    }
    if (dirty) X86_MEM8(view + kDirty) = 1;
}

// the screen's width as the game takes it (640, or its render target's): only the 640 wide frame is in the 16:9
// picture
uint32_t FrameWidth(uint8_t* base, uint32_t view) {
    uint32_t target = X86_MEM32(view + 0x128), flags = X86_MEM32(0x50C2B8);
    return flags && X86_MEM8(flags + 0x34) && target && (X86_MEM8(target + 0x1C) & 8) ? 640 : target ? X86_MEM32(target + 8) : 0;
}

// a camera of the 16:9 picture: its aspect 16:9 on the 640 wide frame
bool In16x9(uint8_t* base, uint32_t view) {
    return app::Widescreen() && X86_MEM32(view + kPerspective) && std::fabs(GetF(base, view + kAspect) - kAspect169) <= 1e-4f &&
           FrameWidth(base, view) == 640;
}

// a view drawn into part of the frame (the minimap, in the bottom right corner), not across its width
bool InPart(uint8_t* base, uint32_t view) {
    if (FrameWidth(base, view) != 640) return false;
    int32_t left = int32_t(X86_MEM32(view + kLeft)), right = int32_t(X86_MEM32(view + kRight));
    return left > 0 || right < int32_t(kFrameW) - 1;
}

// A view drawn into part of the frame is in 3D: the 16:9 picture shows it where its rectangle is across the whole
// screen, but the HUD drawn over it (its pins, its N, placed through WorldToScreen into that rectangle) is drawn flat,
// kept 4:3 in the middle: nearer the middle by a quarter of its distance from it, and in the close view a tenth
// bigger about the middle. The view's rectangle is moved there too, and made as much bigger; its width already is the
// narrowed one, as its camera is the widened one. In the frame's pixels, last row and column included; false for a
// view across the whole frame.
bool PartRect(uint8_t* base, uint32_t view, int32_t out[4]) {
    // the camera Apply widens (the game sets its 4:3 again every frame, before or after this)
    float aspect = GetF(base, view + kAspect);
    if (!app::Widescreen() || !X86_MEM32(view + kPerspective) || !InPart(base, view) ||
        (std::fabs(aspect - kAspect43) > 1e-4f && std::fabs(aspect - kAspect169) > 1e-4f))
        return false;
    const double k = HudSize();
    auto place = [&](uint32_t lo_at, uint32_t hi_at, double frame, double narrowed, int32_t& lo, int32_t& hi) {
        int32_t a = int32_t(X86_MEM32(view + lo_at)), b = int32_t(X86_MEM32(view + hi_at));
        double middle = (a + b + 1) * 0.5, half = (b + 1 - a) * 0.5 * k;
        middle = frame * 0.5 + (middle - frame * 0.5) * narrowed * k;
        lo = int32_t(std::lround(middle - half));
        hi = int32_t(std::lround(middle + half)) - 1;
    };
    place(kLeft, kRight, kFrameW, kNarrowed, out[0], out[2]);
    place(kTop, kBottom, kFrameH, 1.0, out[1], out[3]);
    return true;
}

}  // namespace

// the projection made from the view state, when it changed (bool argument: only mark it changed)
X86_FUNC(sub_00343930) {
    if (uint32_t view = X86_MEM32(kView)) {
        Apply(base, view);
    }
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
// narrowed to 4:3 in the middle (gl/gl_backend.cpp), in the close view also made a tenth bigger, while the point it
// marks is in the 16:9 picture: narrowed twice, the icons slid towards the middle and moved more slowly than the map
// under them. The point is moved here by the opposite of what the flat drawing goes through, so it lands back on its
// point. In a view in part of the frame (the minimap), whose rectangle PartRect moves and enlarges as the HUD, only
// its own widened camera's x is.
X86_FUNC(sub_00343FF0) {
    uint32_t out = X86_MEM32(ctx.esp + 4);
    __imp__sub_00343FF0(ctx, base);
    uint32_t view = X86_MEM32(kView);
    if (!out || !view || !In16x9(base, view)) return;
    float x = GetF(base, out), y = GetF(base, out + 4);
    if (InPart(base, view)) {
        SetF(base, out, kFrameW * 0.5f + (x - kFrameW * 0.5f) * (kAspect169 / kAspect43));
        return;
    }
    const float k = HudSize();
    SetF(base, out, kFrameW * 0.5f + (x - kFrameW * 0.5f) / (kNarrowed * k));
    if (k != 1.0f) SetF(base, out + 4, kFrameH * 0.5f + (y - kFrameH * 0.5f) / k);
}

// The view's screen scale and offset for its vertex shaders (0x003443A0(float, float), from its rectangle): for a
// view in part of the frame, made from its rectangle as PartRect places it, so that its 3D lands under the HUD placed
// over it. The rectangle itself stays, for what the game places in it.
X86_FUNC(sub_003443A0) {
    uint32_t view = X86_MEM32(kView);
    int32_t r[4], kept[4];
    bool moved = view && PartRect(base, view, r);
    if (moved) {
        const uint32_t at[4] = {kLeft, kTop, kRight, kBottom};
        for (int i = 0; i < 4; i++) {
            kept[i] = int32_t(X86_MEM32(view + at[i]));
            X86_MEM32(view + at[i]) = uint32_t(r[i]);
        }
        LOG_FIRST(4, "sm2: a view in part of the frame (%d,%d)-(%d,%d) drawn at (%d,%d)-(%d,%d) for the 16:9 picture", kept[0], kept[1],
                  kept[2], kept[3], r[0], r[1], r[2], r[3]);
    }
    __imp__sub_003443A0(ctx, base);
    if (moved) {
        const uint32_t at[4] = {kLeft, kTop, kRight, kBottom};
        for (int i = 0; i < 4; i++) X86_MEM32(view + at[i]) = uint32_t(kept[i]);
    }
}
