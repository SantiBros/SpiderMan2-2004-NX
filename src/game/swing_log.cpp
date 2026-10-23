// Spider-Man 2's web swing, in the log. The swing controller (Spider-Man + 0x185C, its method at 0x0011C4D0, five
// arguments, the last true at a new swing) works out an angle from the web's anchor and his orientation, and from it
// two of the eight SWING_V_0 to SWING_V_315 animations to blend (+0x14, +0x18) and the variant (A to D, +0x0C) of the
// charge, break, jump and land animations. At a new swing it also draws lots (rand() < 0.5, +0x21) for the second set,
// SWING_V_..._B, which it plays while the swing allows it (+0x20). A `swing:` line for each swing, when the next one
// starts (the first 150 of a start, one line each so the card is not written to as the game swings): the set it drew,
// the poses it went through and how often they changed. A log of swings that never changes them says where to look.
#include <algorithm>
#include <cstring>

#include "core/log.h"
#include "cpu/x86_ops.h"

X86_EXTERN_FUNC(__imp__sub_0011C4D0);

namespace {

struct Swing {
    bool on = false, b = false, allowed = true;
    uint32_t low = ~0u, high = 0, changes = 0, last = ~0u;
    float angle0 = 0, angle1 = 0;
};
Swing g_swing;
uint32_t g_lines = 0;

const uint32_t kDegrees[] = {0, 0, 45, 90, 135, 180, 225, 270, 315, 0};

void Report(const Swing& w) {
    if (!w.on || g_lines >= 150) return;
    g_lines++;
    LOGI("swing: set %s%s, poses SWING_V_%u to SWING_V_%u, %u changes, angle %.2f to %.2f", w.b ? "B" : "A",
         w.allowed ? "" : " (B not allowed)", kDegrees[std::min(w.low, 9u)], kDegrees[std::min(w.high, 9u)], w.changes,
         w.angle0, w.angle1);
}

}  // namespace

X86_FUNC(sub_0011C4D0) {
    uint32_t self = ctx.ecx;
    bool start = X86_MEM8(ctx.esp + 0x14) != 0;
    __imp__sub_0011C4D0(ctx, base);
    if (g_lines >= 150) return;
    uint32_t a = X86_MEM32(self + 0x14), b = X86_MEM32(self + 0x18);
    bool allowed = X86_MEM8(self + 0x20) != 0, drawn = X86_MEM8(self + 0x21) != 0;
    float angle;
    std::memcpy(&angle, &X86_MEM32(self + 8), 4);
    Swing& w = g_swing;
    if (start) {
        Report(w);
        w = Swing{};
        w.on = true;
        w.angle0 = angle;
        w.b = allowed && drawn;  // what the new swing plays
        w.allowed = allowed;
    }
    if (!w.on) return;
    uint32_t key = (a & 0xFF) << 8 | (b & 0xFF);
    if (key != w.last) {
        if (w.last != ~0u) w.changes++;
        w.last = key;
    }
    w.low = std::min({w.low, a, b});
    w.high = std::max({w.high, a, b});
    w.angle1 = angle;
}
