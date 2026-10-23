// The host side on the Switch: settings at their defaults, the .nro's folder as the exe folder (main chdirs there).
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>

#include "app/app.h"
#include "switch/settings.h"
#include "switch/state.h"

namespace app {

Settings& Get() {
    static Settings s;
    return s;
}
void Load() {}
void Save() {}
void ApplyDisplay() {}
Mode Desktop() { return {1280, 720}; }
Mode Output() { return Desktop(); }
float RenderScale(uint32_t) { return 1.0f; }
// the game's own 16:9 picture (src/game/widescreen.cpp), from the debug menu's Screen
bool Widescreen() { return sw::g_screen.load(std::memory_order_relaxed) == int(sw::Screen::Wide); }

std::filesystem::path ExeDir() {
    static const std::filesystem::path dir = std::filesystem::current_path();
    return dir;
}
int Instance() { return 0; }
void* Window() { return nullptr; }

bool OpenSplash() { return false; }
void SplashFirstFrame() {}
void CloseSplash() {}
void OpenGameWindow(bool) {}
bool StartHandover() { return false; }
bool HandingOver() { return false; }
void FinishHandover() {}

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

void Pace() {}

}  // namespace app
