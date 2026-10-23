// Spider-Man 2 (Xbox, title 4156002B) hooks for default.xbe.
// A strong definition replaces the recompiled function at that address.
#include <cstring>

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

// XGetFilePhysicalSortKey(HANDLE) -> ULONG. The XAPI body asks the kernel for the volume's file system
// (IoQueryVolumeInformation) and reads the starting sector or cluster out of the file object, which the host
// does not model. The key only orders reads by position on the disc, so every file gets the same one.
HLE_STDCALL(XGetFilePhysicalSortKey, 1) { return a[0] && a[0] != 0xFFFFFFFF ? 0 : 0xFFFFFFFF; }

// XInputGetCapabilities(HANDLE, XINPUT_CAPABILITIES*). The symbol finder does not know it, so it is named here. The
// XAPI body asks the USB driver about the device behind the handle, which the host's handles (xdk/xapi.cpp) are not:
// it returned no capabilities, and a pad without rumble motors is one the game never shakes (its SetVibration,
// 0x00287900, first checks the motors this reports). Every pad is a standard Xbox controller with both motors.
#define SYM_XInputGetCapabilities 00445438
HLE_STDCALL(XInputGetCapabilities, 2) {
    uint32_t caps = a[1];
    if (!a[0] || !caps) return 6;  // ERROR_INVALID_HANDLE
    // packed: SubType, Reserved, In.Gamepad (buttons, 8 analog buttons, 4 thumbs), Out.Rumble (left, right motor)
    std::memset(HostPtr(caps), 0, 25);
    *static_cast<uint8_t*>(HostPtr(caps)) = 1;  // XINPUT_DEVSUBTYPE_GC_GAMEPAD
    W16(caps + 3, 0x00FF);
    std::memset(HostPtr(caps + 5), 0xFF, 8);
    for (uint32_t i = 0; i < 4; i++) W16(caps + 13 + 2 * i, 0xFFFF);
    W16(caps + 21, 0xFFFF);
    W16(caps + 23, 0xFFFF);
    LOG_FIRST(4, "sm2: XInputGetCapabilities: a standard controller with both rumble motors");
    return 0;
}
