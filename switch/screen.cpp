#include "switch/screen.h"

#include <switch.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>

namespace sw {

namespace {

std::mutex g_lock;
bool g_text = false;
bool g_fb_ready = false;
Framebuffer g_fb;
constexpr uint32_t kWidth = 1280, kHeight = 720;

void OpenFramebuffer() {
    if (g_fb_ready) return;
    if (g_text) {
        consoleExit(nullptr);
        g_text = false;
    }
    framebufferCreate(&g_fb, nwindowGetDefault(), kWidth, kHeight, PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&g_fb);
    g_fb_ready = true;
}

}  // namespace

void Message(const std::string& title, const std::string& detail, const std::string& footer) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_fb_ready) return;  // the game is on screen; messages go to the log
    if (!g_text) {
        consoleInit(nullptr);
        g_text = true;
    }
    consoleClear();
    std::printf("\n  %s\n  ", title.c_str());
    for (size_t i = 0; i < title.size(); i++) std::putchar('-');
    std::printf("\n\n");
    // indent every line of the detail
    std::printf("  ");
    for (char c : detail) {
        std::putchar(c);
        if (c == '\n') std::printf("  ");
    }
    std::printf("\n\n  %s\n\n  Spider-Man 2 (Xbox) for Switch - test build by SantiBros\n", footer.c_str());
    consoleUpdate(nullptr);
}

void CloseText() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_text) consoleExit(nullptr);
    g_text = false;
}

void Frame(const uint32_t* rgba, uint32_t w, uint32_t h) {
    std::lock_guard<std::mutex> lock(g_lock);
    OpenFramebuffer();
    uint32_t stride = 0;
    uint32_t* dst = static_cast<uint32_t*>(framebufferBegin(&g_fb, &stride));
    stride /= 4;
    // fit the frame's height to the screen, keep its shape, black bars at the sides
    uint32_t oh = kHeight, ow = std::min<uint32_t>(kWidth, w * kHeight / std::max<uint32_t>(h, 1));
    uint32_t ox = (kWidth - ow) / 2;
    for (uint32_t y = 0; y < kHeight; y++) {
        uint32_t* row = dst + size_t(y) * stride;
        std::fill(row, row + ox, 0xFF000000u);
        std::fill(row + ox + ow, row + kWidth, 0xFF000000u);
        const uint32_t* src = rgba + size_t(std::min(h - 1, y * h / oh)) * w;
        for (uint32_t x = 0; x < ow; x++) row[ox + x] = src[std::min(w - 1, x * w / ow)];
    }
    framebufferEnd(&g_fb);
}

void Close() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_fb_ready) framebufferClose(&g_fb);
    if (g_text) consoleExit(nullptr);
    g_fb_ready = g_text = false;
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
