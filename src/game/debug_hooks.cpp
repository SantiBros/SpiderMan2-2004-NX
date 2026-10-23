// Spider-Man 2 hooks that only log, for finding things in the test build (not the Switch's): each one off unless its
// variable is set.
#if !defined(__SWITCH__)
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"

X86_EXTERN_FUNC(__imp__sub_00397CB4);

// RANDLOG=N: the first N values the C library's rand() gives, with the caller and the thread's seed
X86_FUNC(sub_00397CB4) {
    static const long n = std::getenv("RANDLOG") ? std::atol(std::getenv("RANDLOG")) : 0;
    static long logged = 0;
    uint32_t from = X86_MEM32(ctx.esp);
    __imp__sub_00397CB4(ctx, base);
    if (logged < n) {
        logged++;
        LOGI("rand: %u from %08X", ctx.eax, from);
    }
}

X86_EXTERN_FUNC(__imp__sub_00397162);

// FMODCHECK=N: the first N results of the C library's x87 fmod (_CIfmod), against the host's
X86_FUNC(sub_00397162) {
    static const long n = std::getenv("FMODCHECK") ? std::atol(std::getenv("FMODCHECK")) : 0;
    static long logged = 0;
    double x = X87_ST(1), y = X87_ST(0);
    uint32_t from = X86_MEM32(ctx.esp);
    __imp__sub_00397162(ctx, base);
    if (logged < n) {
        logged++;
        LOGI("fmod: %g %g -> %g (host %g) from %08X", x, y, X87_ST(0), std::fmod(x, y), from);
    }
}

// RUMBLETRACE=N: the first N starts and updates of the game's rumble effects (0x002BCFC0 starts one at a strength,
// 0x002BD0D0 and 0x002BD1C0 update or shape it), with the caller, the strength asked and the effect's on flag
#define SM2_RUMBLE_TRACE(addr, label)                                                                                \
    X86_EXTERN_FUNC(__imp__sub_##addr);                                                                            \
    X86_FUNC(sub_##addr) {                                                                                         \
        static const long n = std::getenv("RUMBLETRACE") ? std::atol(std::getenv("RUMBLETRACE")) : 0;             \
        static long logged = 0;                                                                                    \
        if (logged < n) {                                                                                          \
            logged++;                                                                                              \
            float v;                                                                                               \
            std::memcpy(&v, &X86_MEM32(ctx.esp + 4), 4);                                                           \
            LOGI("rumble effect %s: %08X strength %.3f on %u from %08X", label, ctx.ecx, v, X86_MEM8(ctx.ecx + 0x5C), \
                 X86_MEM32(ctx.esp));                                                                              \
        }                                                                                                          \
        __imp__sub_##addr(ctx, base);                                                                              \
    }
SM2_RUMBLE_TRACE(002BCFC0, "start")
SM2_RUMBLE_TRACE(002BD0D0, "update")
SM2_RUMBLE_TRACE(002BD1C0, "shape")

X86_EXTERN_FUNC(__imp__sub_0028E810);

// RUMBLETEST=N: at an open pad's Nth update (0x0028E810, each pad each frame) the pad's own SetVibration(1.0)
// (0x00287900, the call the game's rumble effects end in), at N+120 SetVibration(0); with the player's rumble effect
// and its on flag. RUMBLELOG=1 shows what reaches the host.
X86_FUNC(sub_0028E810) {
    static const long n = std::getenv("RUMBLETEST") ? std::atol(std::getenv("RUMBLETEST")) : 0;
    static long calls = 0;
    uint32_t pad = ctx.ecx;
    __imp__sub_0028E810(ctx, base);
    if (!n || !X86_MEM32(pad + 0x50)) return;  // only a pad that is open
    calls++;
    if (calls != n && calls != n + 120) return;
    uint32_t player = X86_MEM32(0x501124), effect = player ? X86_MEM32(player + 4) : 0;
    LOGI("rumble test: pad %08X motors %u %u, player %08X effect %08X on %u", pad, X86_MEM16(pad + 0x49),
         X86_MEM16(pad + 0x4B), player, effect, effect ? X86_MEM8(effect + 0x5C) : 0);
    X86Context saved = ctx;
    float v = calls == n ? 1.0f : 0.0f;
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    ctx.ecx = pad;
    kernel::CallGuest(ctx, 0x287900, {bits});
    ctx = saved;
}

#endif  // !__SWITCH__
