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

// log sync every 250 ms and a pulse line every 2 s, on threads of their own
void StartWatch();

}  // namespace sw
