#pragma once
#include <atomic>
#include <cstdint>

// Counters the Switch files share for the pulse line, and the start-up switches.
namespace sw {

struct Stats {
    std::atomic<uint64_t> frames{0};  // presented by the game
    std::atomic<uint64_t> drawn{0};   // of those, drawn by the CPU renderer
    std::atomic<uint64_t> shown{0};   // of those, put on screen
    std::atomic<uint64_t> draws{0};   // draw calls the game made
    std::atomic<uint64_t> sound{0};   // sound buffers played
};
inline Stats g_stats;

inline bool g_no_sound = false;  // L held at start
inline std::atomic<float> g_volume{1.0f};  // the game's sound, from the settings
// the picture as the settings have it, for the renderer's thread and the game's (Settings::resolution, screen, sharp)
inline std::atomic<uint32_t> g_resolution{480};
inline std::atomic<int> g_screen{0};
inline std::atomic<bool> g_sharp{false};
inline std::atomic<uint32_t> g_anisotropy{1};

// log sync every 250 ms and a pulse line every 2 s, on threads of their own
void StartWatch();

}  // namespace sw
