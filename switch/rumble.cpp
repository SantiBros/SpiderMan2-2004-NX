#include "switch/rumble.h"

#include <switch.h>

#include <algorithm>
#include <atomic>
#include <cmath>

#include "core/log.h"
#include "switch/settings.h"

namespace sw::rumble {

namespace {

// the game's last request: the Xbox's left motor (heavy, low) in the high half, the right (light, high) in the low
std::atomic<uint32_t> g_want{0};
std::atomic<uint64_t> g_changed{0};  // when it last changed, ms

// a rumble the game leaves on unchanged this long dies away over kFadeMs: no controller left shaking on its own
constexpr uint64_t kHoldMs = 2000, kFadeMs = 600;
// a value is sent again this often while it is not 0, in case the controller let it drop
constexpr uint64_t kResendMs = 250;
// the HD rumble's bands for the Xbox's two motors
constexpr float kLowHz = 160.0f, kHighHz = 320.0f;

uint64_t NowMs() { return armTicksToNs(armGetSystemTick()) / 1000000; }

// the controllers player 1 may hold: a Pro Controller, two Joy-Con, the console's own handheld Joy-Con
struct Device {
    HidNpadIdType id;
    HidNpadStyleTag style;
    HidVibrationDeviceHandle handles[2];
    bool ok = false;
};
Device g_devices[] = {{HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey, {}},
                      {HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual, {}},
                      {HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld, {}}};
bool g_init = false;
float g_sent_low = 0, g_sent_high = 0;
uint64_t g_sent_at = 0;

float Strength(Rumble r) {
    switch (r) {
        case Rumble::Low: return 0.25f;
        case Rumble::Medium: return 0.45f;
        case Rumble::High: return 0.7f;
        default: return 0.0f;
    }
}

void Send(float low, float high, uint64_t now) {
    if (!g_init) {
        g_init = true;
        int n = 0;
        for (Device& d : g_devices) {
            d.ok = R_SUCCEEDED(hidInitializeVibrationDevices(d.handles, 2, d.id, d.style));
            n += d.ok;
        }
        LOGI("rumble: %d of 3 controller kinds ready", n);
    }
    HidVibrationValue v[2];
    for (HidVibrationValue& x : v) x = {low, kLowHz, high, kHighHz};
    for (Device& d : g_devices) {
        if (!d.ok || !(hidGetNpadStyleSet(d.id) & d.style)) continue;
        hidSendVibrationValues(d.handles, v, 2);
    }
    g_sent_low = low;
    g_sent_high = high;
    g_sent_at = now;
}

}  // namespace

void Set(uint16_t left, uint16_t right) {
    uint32_t v = uint32_t(left) << 16 | right;
    if (g_want.exchange(v, std::memory_order_relaxed) != v) g_changed.store(NowMs(), std::memory_order_relaxed);
}

void Update(bool quiet) {
    uint64_t now = NowMs();
    uint32_t want = g_want.load(std::memory_order_relaxed);
    float k = quiet ? 0.0f : Strength(CurrentSettings().rumble);
    float low = k * float(want >> 16) / 65535.0f, high = k * float(want & 0xFFFF) / 65535.0f;
    // left on: held kHoldMs, then down to nothing
    uint64_t held = now - g_changed.load(std::memory_order_relaxed);
    if (want && held > kHoldMs) {
        float f = 1.0f - std::min(1.0f, float(held - kHoldMs) / float(kFadeMs));
        low *= f;
        high *= f;
    }
    // to a 64th, so a changing request does not send every frame for nothing
    low = std::round(low * 64.0f) / 64.0f;
    high = std::round(high * 64.0f) / 64.0f;
    bool zero = low == 0 && high == 0, was_zero = g_sent_low == 0 && g_sent_high == 0;
    if (low != g_sent_low || high != g_sent_high || (!zero && now - g_sent_at >= kResendMs)) {
        if (!(zero && was_zero && g_init)) Send(low, high, now);
    }
}

void Stop() {
    g_want.store(0, std::memory_order_relaxed);
    if (g_init) Send(0, 0, NowMs());
}

}  // namespace sw::rumble
