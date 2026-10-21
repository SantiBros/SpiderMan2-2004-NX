#include "switch/screen.h"

#include <switch.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

namespace sw {

namespace {

constexpr uint32_t kWidth = 1280, kHeight = 720;
constexpr uint32_t kBack = 0xFF301A10;   // dark blue, ABGR
constexpr uint32_t kText = 0xFFFFFFFF;
constexpr uint32_t kTitle = 0xFF40C8FF;  // amber

Framebuffer g_fb;
bool g_fb_ready = false;

std::mutex g_lock;  // guards what is waiting for the next Pump, never held while presenting
std::string g_title, g_detail, g_footer;
bool g_text_dirty = false;
std::vector<uint32_t> g_frame;  // the newest game frame
uint32_t g_frame_w = 0, g_frame_h = 0;
bool g_frame_dirty = false;
std::atomic<bool> g_game{false};

// libnx's console font: 256 glyphs of 16x16, each 16 rows of 16 bits with the leftmost pixel in the top bit
const uint16_t* Font() { return static_cast<const uint16_t*>(consoleGetDefault()->font.gfx); }

void Glyph(uint32_t* dst, uint32_t stride, int x, int y, uint8_t c, uint32_t color) {
    if (x < 0 || y < 0 || x + 16 > int(kWidth) || y + 16 > int(kHeight)) return;
    const uint16_t* g = Font() + 16 * c;
    for (int r = 0; r < 16; r++)
        for (int i = 0; i < 16; i++)
            if (g[r] & (0x8000 >> i)) dst[size_t(y + r) * stride + x + i] = color;
}

// one line, cut at the screen's edge
int Line(uint32_t* dst, uint32_t stride, int x, int y, const std::string& s, uint32_t color) {
    for (size_t i = 0; i < s.size() && x + 16 <= int(kWidth); i++, x += 16) Glyph(dst, stride, x, y, uint8_t(s[i]), color);
    return y + 18;
}

void DrawText(uint32_t* dst, uint32_t stride, const std::string& title, const std::string& detail, const std::string& footer) {
    for (uint32_t y = 0; y < kHeight; y++) std::fill(dst + size_t(y) * stride, dst + size_t(y) * stride + kWidth, kBack);
    int y = 32;
    y = Line(dst, stride, 32, y, title, kTitle);
    y = Line(dst, stride, 32, y, std::string(title.size(), '-'), kTitle) + 18;
    size_t start = 0;
    while (start <= detail.size() && y < int(kHeight) - 90) {
        size_t end = detail.find('\n', start);
        if (end == std::string::npos) end = detail.size();
        std::string line = detail.substr(start, end - start);
        // long lines wrap at 76 characters
        for (size_t p = 0; p < line.size() || p == 0; p += 76) {
            y = Line(dst, stride, 32, y, line.substr(p, 76), kText);
            if (line.empty()) break;
        }
        start = end + 1;
    }
    Line(dst, stride, 32, int(kHeight) - 72, footer, kText);
    Line(dst, stride, 32, int(kHeight) - 48, "Spider-Man 2 (Xbox) for Switch - test build by SantiBros", kTitle);
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

}  // namespace

void Init() {
    if (g_fb_ready) return;
    if (R_SUCCEEDED(framebufferCreate(&g_fb, nwindowGetDefault(), kWidth, kHeight, PIXEL_FORMAT_RGBA_8888, 2))) {
        framebufferMakeLinear(&g_fb);
        g_fb_ready = true;
    }
}

void Message(const std::string& title, const std::string& detail, const std::string& footer) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_title = title;
    g_detail = detail;
    g_footer = footer;
    g_text_dirty = true;
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
    if (!g_fb_ready) return;
    static std::vector<uint32_t> frame;
    static std::string title, detail, footer;
    uint32_t w = 0, h = 0;
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
    if (!text && !game) return;
    uint32_t stride = 0;
    auto* dst = static_cast<uint32_t*>(framebufferBegin(&g_fb, &stride));
    if (!dst) return;
    stride /= 4;
    if (game)
        DrawFrame(dst, stride, frame.data(), w, h);
    else
        DrawText(dst, stride, title, detail, footer);
    framebufferEnd(&g_fb);
}

bool ShowingGame() { return g_game; }

void Close() {
    if (g_fb_ready) framebufferClose(&g_fb);
    g_fb_ready = false;
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

}  // namespace sw
