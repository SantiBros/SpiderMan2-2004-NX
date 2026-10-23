// Spider-Man 2's web swing, in the log. The swing controller (Spider-Man + 0x185C, its method at 0x0011C4D0, five
// arguments, the last true at a new swing) works out an angle from the web's anchor and his orientation, and from it
// two of the eight SWING_V_0 to SWING_V_315 animations to blend (+0x14, +0x18) and the variant (A to D, +0x0C) of the
// charge, break, jump and land animations. At a new swing it also draws lots (rand() < 0.5, +0x21) for the second set,
// SWING_V_..._B, which it plays while the swing allows it (+0x20). Each new swing and each change of what it plays is
// a `swing:` line, the first 400 of a start: a log of swings that never changes them says where to look.
#include <cstring>

#include "core/log.h"
#include "cpu/x86_ops.h"

X86_EXTERN_FUNC(__imp__sub_0011C4D0);

X86_FUNC(sub_0011C4D0) {
    static uint32_t last = ~0u, lines = 0;
    uint32_t self = ctx.ecx;
    bool start = X86_MEM8(ctx.esp + 0x14) != 0;
    __imp__sub_0011C4D0(ctx, base);
    if (lines >= 400) return;
    uint32_t a = X86_MEM32(self + 0x14), b = X86_MEM32(self + 0x18), variant = X86_MEM32(self + 0x0C);
    bool allowed = X86_MEM8(self + 0x20) != 0, drawn = X86_MEM8(self + 0x21) != 0;
    uint32_t key = (a & 0xFF) << 16 | (b & 0xFF) << 8 | (variant & 3) << 2 | uint32_t(allowed && drawn);
    if (!start && key == last) return;
    last = key;
    lines++;
    float angle;
    std::memcpy(&angle, &X86_MEM32(self + 8), 4);
    LOGI("swing: %sangle %.2f, animations %u and %u, variant %c, set %s%s", start ? "new swing, " : "", angle, a, b,
         char('A' + (variant & 3)), allowed && drawn ? "B" : "A", allowed ? "" : " (B not allowed)");
}
