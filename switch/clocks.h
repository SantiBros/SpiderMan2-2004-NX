#pragma once
#include <cstdint>

// The Switch's CPU, GPU and memory clocks, for the debug menu: set through clkrst (pcv before firmware 8.0), the
// services sys-clk uses, and put back when the system resets them (docking, sleep). The system's own clocks come
// back while the port is in the HOME menu and when it closes. 0 is the system's own clock.
namespace sw::clocks {

bool Supported();
void Set(uint32_t cpu_hz, uint32_t gpu_hz, uint32_t mem_hz);
// the clocks running now; false when they can't be read
bool Get(uint32_t& cpu_hz, uint32_t& gpu_hz, uint32_t& mem_hz);
// main thread, every frame or so: the chosen clocks back after the system reset them
void Tick();
// closing: the system's clocks back, and the exit unlocked
void Exit();
bool Docked();

}  // namespace sw::clocks
