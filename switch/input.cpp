// The Switch's controls as the console's controller on port 0. Face buttons go by position, as the game's prompts
// show the Xbox letters: Xbox A (bottom) is the Switch's B, Xbox B (right) is A, Xbox X (left) is Y, Xbox Y (top)
// is X. L and R are the Xbox's white and black buttons, ZL and ZR the triggers, + is Start and - is Back. The debug
// menu's combination (ZL + ZR + -) and the menu itself don't reach the game. The game's rumble goes to switch/rumble.
#include <switch.h>

#include <mutex>

#include "app/app.h"
#include "core/input.h"
#include "switch/menu.h"
#include "switch/rumble.h"

namespace input {

namespace {
std::mutex g_lock;
PadState g_pad;
bool g_init = false;
}  // namespace

void Init() {}

Pad Poll(int port) {
    Pad p;
    p.connected = port == 0;
    if (port == 0) {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_init) {
            padInitializeDefault(&g_pad);
            g_init = true;
        }
        padUpdate(&g_pad);
        uint64_t b = padGetButtons(&g_pad);
        // the debug menu's: nothing for the game while it is open, and not the - of its combination
        bool hide_back = false;
        if (sw::menu::Blocking(hide_back)) {
            if (app::kGame.pad) app::kGame.pad(port, p);
            return p;
        }
        if (hide_back) b &= ~uint64_t(HidNpadButton_Minus);
        if (b & HidNpadButton_Up) p.buttons |= 1;
        if (b & HidNpadButton_Down) p.buttons |= 2;
        if (b & HidNpadButton_Left) p.buttons |= 4;
        if (b & HidNpadButton_Right) p.buttons |= 8;
        if (b & HidNpadButton_Plus) p.buttons |= 16;
        if (b & HidNpadButton_Minus) p.buttons |= 32;
        if (b & HidNpadButton_StickL) p.buttons |= 64;
        if (b & HidNpadButton_StickR) p.buttons |= 128;
        auto press = [&](int i, uint64_t mask) { p.analog[i] = (b & mask) ? 255 : 0; };
        press(0, HidNpadButton_B);   // Xbox A
        press(1, HidNpadButton_A);   // Xbox B
        press(2, HidNpadButton_Y);   // Xbox X
        press(3, HidNpadButton_X);   // Xbox Y
        press(4, HidNpadButton_R);   // black
        press(5, HidNpadButton_L);   // white
        press(6, HidNpadButton_ZL);  // left trigger
        press(7, HidNpadButton_ZR);  // right trigger
        HidAnalogStickState l = padGetStickPos(&g_pad, 0), r = padGetStickPos(&g_pad, 1);
        auto clamp = [](int v) { return int16_t(v > 32767 ? 32767 : v < -32767 ? -32767 : v); };
        p.lx = clamp(l.x);
        p.ly = clamp(l.y);
        p.rx = clamp(r.x);
        p.ry = clamp(r.y);
    }
    if (app::kGame.pad) app::kGame.pad(port, p);
    return p;
}

void Rumble(int port, uint16_t left, uint16_t right) {
    if (app::kGame.rumble) app::kGame.rumble(port, left, right);
    if (port == 0) sw::rumble::Set(left, right);
}

}  // namespace input
