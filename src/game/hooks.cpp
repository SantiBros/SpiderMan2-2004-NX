// Spider-Man 2 (Xbox, title 4156002B) hooks for default.xbe.
// A strong definition replaces the recompiled function at that address.
#include "core/log.h"
#include "xdk/hle.h"

using namespace kernel;

// D3DDevice_Reset(D3DPRESENT_PARAMETERS*). The game calls it right after Direct3D_CreateDevice. The XDK body frees
// the frame buffers through the guest heap, but the host made them, so the original crashes in the heap code.
// The host device already matches the parameters the game passes (640x480), so accept them.
HLE_STDCALL(D3DDevice_Reset, 1) {
    uint32_t pp = a[0];
    if (pp)
        LOGI("sm2: D3DDevice_Reset %ux%u format %X buffers %u flags %X interval %X (kept the host device)", R32(pp),
             R32(pp + 4), R32(pp + 8), R32(pp + 12), R32(pp + 0x28), R32(pp + 0x30));
    return 0;  // D3D_OK
}
