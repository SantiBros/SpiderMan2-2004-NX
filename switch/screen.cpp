#include "switch/screen.h"

#include <switch.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include "core/log.h"
#include "gl/gl_api.h"
#include "gl/gl_backend.h"
#include "switch/state.h"

namespace sw {

namespace {

constexpr uint32_t kWidth = 1280, kHeight = 720;
constexpr uint32_t kBack = 0xFF301A10;   // dark blue, ABGR
constexpr uint32_t kText = 0xFFFFFFFF;
constexpr uint32_t kTitle = 0xFF40C8FF;  // amber
constexpr uint32_t kGray = 0xFFA8A8A8;

enum class Mode { None, Framebuffer, Gl };
std::atomic<Mode> g_mode{Mode::None};
std::string g_info = "none";

// framebuffer mode
Framebuffer g_fb;

// GL mode
EGLDisplay g_dpy = EGL_NO_DISPLAY;
EGLSurface g_surface = EGL_NO_SURFACE;
EGLContext g_ctx = EGL_NO_CONTEXT;
bool g_main_gl = false;                // the main thread holds the context
std::atomic<bool> g_gl_wanted{false};  // the renderer asked for it
std::atomic<bool> g_gl_released{false};
std::vector<uint32_t> g_canvas;        // the text screen, drawn on the CPU and shown as an image

std::mutex g_lock;  // guards what is waiting for the next Pump, never held while presenting
std::string g_title, g_detail, g_footer;
bool g_text_dirty = false;
std::vector<uint32_t> g_frame;  // the newest game frame
uint32_t g_frame_w = 0, g_frame_h = 0;
bool g_frame_dirty = false;
std::atomic<bool> g_game{false};

// libnx's console font: 256 glyphs of 16x16, each 16 rows of 16 bits with the leftmost pixel in the top bit
const uint16_t* Font() { return static_cast<const uint16_t*>(consoleGetDefault()->font.gfx); }

void Glyph(uint32_t* dst, uint32_t stride, int x, int y, uint8_t c, uint32_t color, uint32_t w = kWidth,
           uint32_t h = kHeight, int scale = 1) {
    if (x < 0 || y < 0 || x + 16 * scale > int(w) || y + 16 * scale > int(h)) return;
    const uint16_t* g = Font() + 16 * c;
    for (int r = 0; r < 16 * scale; r++)
        for (int i = 0; i < 16 * scale; i++)
            if (g[r / scale] & (0x8000 >> (i / scale))) dst[size_t(y + r) * stride + x + i] = color;
}

// the overlays: what the main thread set (under g_lock), and each drawer's copy
struct Layer {
    std::vector<uint32_t> px;
    uint32_t w = 0, h = 0;
    int x = 0, y = 0;
    uint64_t version = 0;
};
Layer g_layers[kLayers];

// a's pixels over dst by their alpha (framebuffer mode)
void Blend(uint32_t* dst, uint32_t stride, const Layer& l) {
    for (uint32_t r = 0; r < l.h; r++) {
        int y = l.y + int(r);
        if (y < 0 || y >= int(kHeight)) continue;
        for (uint32_t c = 0; c < l.w; c++) {
            int x = l.x + int(c);
            if (x < 0 || x >= int(kWidth)) continue;
            uint32_t s = l.px[size_t(r) * l.w + c], a = s >> 24;
            if (!a) continue;
            uint32_t& d = dst[size_t(y) * stride + x];
            if (a == 255) {
                d = s | 0xFF000000u;
                continue;
            }
            uint32_t out = 0xFF000000u;
            for (int k = 0; k < 24; k += 8) {
                uint32_t sv = (s >> k) & 255, dv = (d >> k) & 255;
                out |= ((sv * a + dv * (255 - a) + 127) / 255) << k;
            }
            d = out;
        }
    }
}

// the newest overlays into `mine` (a drawer's own copies): true when any changed
bool TakeLayers(Layer (&mine)[kLayers], bool (&changed)[kLayers]) {
    bool any = false;
    std::lock_guard<std::mutex> lock(g_lock);
    for (int i = 0; i < kLayers; i++) {
        changed[i] = g_layers[i].version != mine[i].version;
        if (changed[i]) mine[i] = g_layers[i];
        any |= changed[i];
    }
    return any;
}

// GL: the overlays over the bound window framebuffer
void DrawLayersGl(const Layer (&mine)[kLayers], const bool (&changed)[kLayers]) {
    for (int i = 0; i < kLayers; i++)
        if (mine[i].w && mine[i].h)
            glr::DrawOverlay(i, mine[i].px.data(), mine[i].w, mine[i].h, mine[i].x, mine[i].y, kWidth, kHeight,
                             changed[i]);
}

// one line, cut at the screen's edge
int Line(uint32_t* dst, uint32_t stride, int x, int y, const std::string& s, uint32_t color) {
    for (size_t i = 0; i < s.size() && x + 16 <= int(kWidth); i++, x += 16) Glyph(dst, stride, x, y, uint8_t(s[i]), color);
    return y + 18;
}

// Loading's screens: their detail starts with this
constexpr char kCentered = '\x01';

void Centered(uint32_t* dst, uint32_t stride, int y, const std::string& s, uint32_t color, int scale = 1) {
    int x = (int(kWidth) - int(s.size()) * 16 * scale) / 2;
    for (size_t i = 0; i < s.size(); i++, x += 16 * scale) Glyph(dst, stride, x, y, uint8_t(s[i]), color, kWidth, kHeight, scale);
}

void DrawText(uint32_t* dst, uint32_t stride, const std::string& title, const std::string& detail, const std::string& footer) {
    for (uint32_t y = 0; y < kHeight; y++) std::fill(dst + size_t(y) * stride, dst + size_t(y) * stride + kWidth, kBack);
    if (!detail.empty() && detail[0] == kCentered) {
        Centered(dst, stride, 288, title, kTitle, 2);
        Centered(dst, stride, 346, detail.substr(1), kText);
        Centered(dst, stride, int(kHeight) - 72, footer, kGray);
        Centered(dst, stride, int(kHeight) - 48, "A project by SantiBros", kTitle);
        return;
    }
    int y = 32;
    y = Line(dst, stride, 32, y, title, kTitle);
    y = Line(dst, stride, 32, y, std::string(title.size(), '-'), kTitle) + 18;
    size_t start = 0;
    while (start <= detail.size() && y < int(kHeight) - 90) {
        size_t end = detail.find('\n', start);
        if (end == std::string::npos) end = detail.size();
        std::string line = detail.substr(start, end - start);
        // long lines wrap at 76 characters, at a space when there is one, indented as they began
        size_t indent = std::min<size_t>(line.find_first_not_of(" -"), 8);
        if (indent == std::string::npos || indent > 8) indent = 0;
        std::string rest = line;
        do {
            size_t cut = rest.size();
            if (cut > 76) {
                cut = rest.rfind(' ', 76);
                if (cut == std::string::npos || cut <= indent) cut = 76;
            }
            y = Line(dst, stride, 32, y, rest.substr(0, cut), kText);
            rest = rest.size() > cut ? std::string(indent, ' ') + rest.substr(rest[cut] == ' ' ? cut + 1 : cut) : "";
        } while (!rest.empty() && y < int(kHeight) - 90);
        start = end + 1;
    }
    Line(dst, stride, 32, int(kHeight) - 72, footer, kText);
    Line(dst, stride, 32, int(kHeight) - 48, "Spider-Man 2 (Xbox) for Switch - a project by SantiBros", kTitle);
}

// fit the frame's height to the screen, keep its shape, black bars at the sides
void DrawFrame(uint32_t* dst, uint32_t stride, const uint32_t* rgba, uint32_t w, uint32_t h) {
    uint32_t oh = kHeight, ow = std::min<uint32_t>(kWidth, w * kHeight / std::max<uint32_t>(h, 1));
    uint32_t ox = (kWidth - ow) / 2;
    for (uint32_t y = 0; y < kHeight; y++) {
        uint32_t* row = dst + size_t(y) * stride;
        std::fill(row, row + ox, 0xFF000000u);
        std::fill(row + ox + ow, row + kWidth, 0xFF000000u);
        const uint32_t* src = rgba + size_t(std::min(h - 1, y * h / oh)) * w;
        for (uint32_t x = 0; x < ow; x++) row[ox + x] = src[std::min(w - 1, x * w / ow)] | 0xFF000000u;
    }
}

bool InitFramebuffer() {
    if (R_FAILED(framebufferCreate(&g_fb, nwindowGetDefault(), kWidth, kHeight, PIXEL_FORMAT_RGBA_8888, 2))) return false;
    framebufferMakeLinear(&g_fb);
    return true;
}

void CloseGl() {
    if (g_dpy == EGL_NO_DISPLAY) return;
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_ctx != EGL_NO_CONTEXT) eglDestroyContext(g_dpy, g_ctx);
    if (g_surface != EGL_NO_SURFACE) eglDestroySurface(g_dpy, g_surface);
    eglTerminate(g_dpy);
    g_dpy = EGL_NO_DISPLAY;
    g_ctx = EGL_NO_CONTEXT;
    g_surface = EGL_NO_SURFACE;
    g_main_gl = false;
}

// an OpenGL 4.3 core context on the window (Mesa's nouveau driver), current on the main thread
bool InitGl(std::string& why) {
    // GL errors are not looked at in play: Mesa skips its checks. A program's machine code is made when it is linked,
    // not at its first draw, so the shader list's programs built ahead are really ready
    setenv("MESA_NO_ERROR", "1", 1);
    setenv("ST_DEBUG", "precompile", 1);
    char buf[160];
    auto fail = [&](const char* what) {
        std::snprintf(buf, sizeof buf, "%s (EGL error %X)", what, unsigned(eglGetError()));
        why = buf;
        CloseGl();
        return false;
    };
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY) return fail("no EGL display");
    if (!eglInitialize(g_dpy, nullptr, nullptr)) {
        g_dpy = EGL_NO_DISPLAY;
        return fail("EGL did not start");
    }
    if (!eglBindAPI(EGL_OPENGL_API)) return fail("no OpenGL in EGL");
    // the window only takes the finished picture: the game draws into textures of its own
    const EGLint cfg_attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                               EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig cfg = nullptr;
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, cfg_attr, &cfg, 1, &n) || !n) return fail("no EGL config");
    g_surface = eglCreateWindowSurface(g_dpy, cfg, nwindowGetDefault(), nullptr);
    if (g_surface == EGL_NO_SURFACE) return fail("no window surface");
    const EGLint ctx_attr[] = {EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                               EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (g_ctx == EGL_NO_CONTEXT) return fail("no OpenGL 4.3 core context");
    if (!eglMakeCurrent(g_dpy, g_surface, g_surface, g_ctx)) return fail("the context did not become current");
    eglSwapInterval(g_dpy, 1);
    const char* missing = nullptr;
    if (!glapi::Load([](const char* name) { return reinterpret_cast<void*>(eglGetProcAddress(name)); }, &missing)) {
        std::snprintf(buf, sizeof buf, "OpenGL function %s missing", missing);
        std::string m = buf;
        CloseGl();
        why = m;
        return false;
    }
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    std::snprintf(buf, sizeof buf, "GL: %s, %s", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                  reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    if (major * 10 + minor < 43) {
        std::string m = std::string(buf) + ": older than OpenGL 4.3";
        CloseGl();
        why = m;
        return false;
    }
    why = buf;
    g_main_gl = true;
    g_canvas.assign(size_t(kWidth) * kHeight, kBack);
    return true;
}

}  // namespace

void Init(bool gl) {
    if (g_mode != Mode::None) return;
    std::string why = "not tried (R held at start)";
    if (gl && InitGl(why)) {
        g_info = why;
        g_mode = Mode::Gl;
        return;
    }
    if (InitFramebuffer()) {
        g_info = "framebuffer, CPU renderer (GL: " + why + ")";
        g_mode = Mode::Framebuffer;
    }
}

const std::string& ScreenInfo() { return g_info; }

void Message(const std::string& title, const std::string& detail, const std::string& footer) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_title = title;
    g_detail = detail;
    g_footer = footer;
    g_text_dirty = true;
}

void Loading(const std::string& title, const std::string& line, const std::string& footer) {
    Message(title, std::string(1, kCentered) + line, footer);
}

void Frame(const uint32_t* rgba, uint32_t w, uint32_t h) {
    if (!w || !h) return;
    std::lock_guard<std::mutex> lock(g_lock);
    g_frame.assign(rgba, rgba + size_t(w) * h);
    g_frame_w = w;
    g_frame_h = h;
    g_frame_dirty = true;
    g_game = true;
}

void Pump() {
    static std::vector<uint32_t> frame;
    static std::string title, detail, footer;
    Mode mode = g_mode;
    if (mode == Mode::Gl) {
        if (!g_main_gl) return;
        if (g_gl_wanted) {
            // the renderer's thread takes the context from here on
            eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            g_main_gl = false;
            g_gl_released = true;
            return;
        }
        static Layer mine[kLayers];
        bool changed[kLayers];
        bool layers = TakeLayers(mine, changed), text = false;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            if (g_text_dirty) {
                title = g_title;
                detail = g_detail;
                footer = g_footer;
                g_text_dirty = false;
                text = true;
            }
        }
        if (!text && !layers) return;
        if (text) DrawText(g_canvas.data(), kWidth, title, detail, footer);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glr::DrawImage(g_canvas.data(), kWidth, kHeight, kWidth, kHeight, text);
        DrawLayersGl(mine, changed);
        eglSwapBuffers(g_dpy, g_surface);
        return;
    }
    if (mode != Mode::Framebuffer) return;
    static uint32_t w = 0, h = 0;
    static Layer mine[kLayers];
    bool changed[kLayers];
    bool layers = TakeLayers(mine, changed);
    bool text = false, game = false;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (g_frame_dirty) {
            frame.swap(g_frame);
            w = g_frame_w;
            h = g_frame_h;
            g_frame_dirty = false;
            game = true;
        } else if (g_text_dirty && !g_game) {
            title = g_title;
            detail = g_detail;
            footer = g_footer;
            text = true;
        }
        g_text_dirty = false;
    }
    if (!text && !game && !layers) return;
    uint32_t stride = 0;
    auto* dst = static_cast<uint32_t*>(framebufferBegin(&g_fb, &stride));
    if (!dst) return;
    stride /= 4;
    // the whole picture each time: the framebuffer's buffers take turns
    if (g_game && !frame.empty())
        DrawFrame(dst, stride, frame.data(), w, h);
    else
        DrawText(dst, stride, title, detail, footer);
    for (const Layer& l : mine)
        if (l.w && l.h) Blend(dst, stride, l);
    framebufferEnd(&g_fb);
}

void Canvas::Reset(uint32_t width, uint32_t height, uint32_t color) {
    w = width;
    h = height;
    px.assign(size_t(w) * h, color);
}

void Canvas::Rect(int x0, int y0, int x1, int y1, uint32_t color) {
    x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, int(w)), y1 = std::min(y1, int(h));
    for (int y = y0; y < y1; y++) std::fill(px.begin() + size_t(y) * w + x0, px.begin() + size_t(y) * w + x1, color);
}

int Canvas::Text(int x, int y, const std::string& s, uint32_t color, int scale) {
    for (size_t i = 0; i < s.size() && x + 16 * scale <= int(w); i++, x += 16 * scale)
        Glyph(px.data(), w, x, y, uint8_t(s[i]), color, w, h, scale);
    return x;
}

int TextWidth(const std::string& s, int scale) { return int(s.size()) * 16 * scale; }

void SetOverlay(int layer, const Canvas& c, int x, int y) {
    if (layer < 0 || layer >= kLayers) return;
    std::lock_guard<std::mutex> lock(g_lock);
    Layer& l = g_layers[layer];
    if (!c.w || !c.h) {
        if (!l.w) return;  // already away
        l.px.clear();
        l.w = l.h = 0;
    } else {
        l.px = c.px;
        l.w = c.w;
        l.h = c.h;
        l.x = x;
        l.y = y;
    }
    l.version++;
}

bool ShowingGame() { return g_game; }

void Close() {
    Mode mode = g_mode;
    if (mode == Mode::Framebuffer) framebufferClose(&g_fb);
    // the renderer's thread may still be drawing with the context: only the main thread's own is closed
    if (mode == Mode::Gl && g_main_gl) CloseGl();
    g_mode = Mode::None;
}

bool QuitRequested() {
    static PadState pad;
    static bool init = false;
    static uint64_t held_since = 0;
    if (!init) {
        padInitializeDefault(&pad);
        init = true;
    }
    padUpdate(&pad);
    uint64_t b = padGetButtons(&pad);
    if ((b & HidNpadButton_Plus) && (b & HidNpadButton_Minus)) {
        uint64_t now = armTicksToNs(armGetSystemTick());
        if (!held_since) held_since = now;
        return now - held_since > 1'000'000'000ull;
    }
    held_since = 0;
    return false;
}

bool UsingGl() { return g_mode == Mode::Gl; }

bool GlTake() {
    if (g_mode != Mode::Gl) return false;
    g_gl_wanted = true;
    uint64_t start = armTicksToNs(armGetSystemTick());
    bool said = false;
    while (!g_gl_released) {
        svcSleepThread(1'000'000);
        if (!said && armTicksToNs(armGetSystemTick()) - start > 3'000'000'000ull) {
            LOGW("screen: still waiting for the main thread to hand over the GL context");
            said = true;
        }
    }
    if (!eglMakeCurrent(g_dpy, g_surface, g_surface, g_ctx)) {
        LOGE("screen: the GL context did not become current on the renderer's thread (EGL error %X)", unsigned(eglGetError()));
        return false;
    }
    return true;
}

void GlRelease() {
    if (g_mode == Mode::Gl) eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

namespace {
// the renderer's thread: the overlays over the frame it just put on the window
void PresentedLayers() {
    static Layer mine[kLayers];
    bool changed[kLayers];
    TakeLayers(mine, changed);
    DrawLayersGl(mine, changed);
}
void PresentedText(uint32_t draws);
}  // namespace

void GlPresented(uint32_t draws) {
    PresentedText(draws);
    PresentedLayers();
}

namespace {
void PresentedText(uint32_t draws) {
    g_stats.frames.fetch_add(1, std::memory_order_relaxed);
    g_stats.drawn.fetch_add(1, std::memory_order_relaxed);
    g_stats.draws.fetch_add(draws, std::memory_order_relaxed);
    if (g_game) {
        g_stats.shown.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (draws) {
        // the game's own picture: the status text goes for good
        LOGI("renderer: first picture with %u draws, the status text goes", draws);
        g_game = true;
        g_stats.shown.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // only clears so far: the newest status text instead of the black frame
    static std::string title, detail, footer;
    static bool drawn_once = false;
    bool dirty = false;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (g_text_dirty) {
            title = g_title;
            detail = g_detail;
            footer = g_footer;
            g_text_dirty = false;
            dirty = true;
        }
    }
    if (dirty) DrawText(g_canvas.data(), kWidth, title, detail, footer);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glr::DrawImage(g_canvas.data(), kWidth, kHeight, kWidth, kHeight, dirty || !drawn_once);
    drawn_once = true;
}
}  // namespace

void GlSwap() { eglSwapBuffers(g_dpy, g_surface); }

void GlSwapInterval(uint32_t blanks) {
    if (g_mode != Mode::Gl) return;
    if (!eglSwapInterval(g_dpy, EGLint(blanks)))
        LOGW("screen: swap interval %u refused (EGL error %X)", blanks, unsigned(eglGetError()));
    else
        LOGI("screen: a frame every %u vertical blanks", blanks);
}

void GlFallBack() {
    if (g_mode != Mode::Gl) return;
    LOGW("screen: back to the framebuffer and the CPU renderer");
    CloseGl();
    if (InitFramebuffer()) {
        g_info = "framebuffer, CPU renderer (GL failed)";
        g_mode = Mode::Framebuffer;
    } else {
        LOGE("screen: no framebuffer either, nothing will show");
        g_mode = Mode::None;
    }
}

}  // namespace sw
