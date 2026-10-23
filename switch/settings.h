#pragma once
#include <cstdint>
#include <string>

// The port's own settings, changed in the debug menu (ZL + ZR + -) and kept in settings.ini beside the .nro.
namespace sw {

enum class Counter { Off, Fps, Full };
// the picture on the screen: 4:3 with black, blurred or web sides, or the game's own 16:9 across it
// (Suit, the web in Spider-Man's suit colours, came last: the values are kept)
enum class Screen { Black, Blur, Web, Wide, Suit };
enum class Rumble { Off, Low, Medium, High };
// a clock setting: 0 the system's own clock, kClockAuto the port's choice for the game (ClocksFor), else Hz
constexpr uint32_t kClockAuto = 0xFFFFFFFFu;

struct Settings {
    uint32_t language = 0;     // 0 the console's; else the game's language code: 1 English, 3 German, 4 French,
                               // 5 Spanish, 6 Italian (the next start)
    uint32_t resolution = 480; // the picture drawn this many pixels tall: 480 as the console, 720, 1080
    Screen screen = Screen::Black;
    bool sharp = false;        // the picture and the movies enlarged with a sharper filter
    uint32_t anisotropy = 1;   // textures' anisotropic filtering: 1 none, 2, 4, 8, 16
    Rumble rumble = Rumble::Medium;
    uint32_t cpu_hz = kClockAuto;  // 0 the system's own clock, kClockAuto the port's choice
    uint32_t gpu_hz = kClockAuto;
    uint32_t mem_hz = kClockAuto;
    Counter counter = Counter::Off;
    uint32_t volume = 100;     // percent of the game's sound
    bool gpu_renderer = true;  // false: the CPU renderer (the next start)
    bool render_thread = true; // the GPU renderer on a thread of its own (the next start)
    bool startup_log = false;  // the newest log lines on screen while the game starts (the next start)
};

// the recommended ones
Settings DefaultSettings();
// the clocks the settings ask for now, in Hz (0 the system's): Auto is 1785 MHz CPU, 1600 MHz memory, and a GPU
// clock for the resolution: 460.8 MHz (Nintendo's handheld top) at 480p in handheld mode, else 768 MHz
void ClocksFor(const Settings& s, bool docked, uint32_t& cpu_hz, uint32_t& gpu_hz, uint32_t& mem_hz);
// what the menu shows and changes, and what this run started with
Settings& CurrentSettings();
const Settings& StartSettings();
// settings.ini in the current folder; missing or unreadable values keep their defaults
void LoadSettings();
void SaveSettings();

}  // namespace sw
