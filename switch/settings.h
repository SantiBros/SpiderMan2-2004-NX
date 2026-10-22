#pragma once
#include <cstdint>
#include <string>

// The port's own settings, changed in the debug menu (ZL + ZR + -) and kept in settings.ini beside the .nro.
namespace sw {

enum class Counter { Off, Fps, Full };

struct Settings {
    uint32_t language = 0;     // 0 the console's; else the game's language code: 1 English, 3 German, 4 French,
                               // 5 Spanish, 6 Italian (the next start)
    uint32_t cpu_hz = 0;       // 0 the system's own clock
    uint32_t gpu_hz = 0;
    uint32_t mem_hz = 0;
    Counter counter = Counter::Off;
    uint32_t volume = 100;     // percent of the game's sound
    bool gpu_renderer = true;  // false: the CPU renderer (the next start)
    bool render_thread = true; // the GPU renderer on a thread of its own (the next start)
    bool startup_log = false;  // the newest log lines on screen while the game starts (the next start)
};

// the recommended ones
Settings DefaultSettings();
// what the menu shows and changes, and what this run started with
Settings& CurrentSettings();
const Settings& StartSettings();
// settings.ini in the current folder; missing or unreadable values keep their defaults
void LoadSettings();
void SaveSettings();

}  // namespace sw
