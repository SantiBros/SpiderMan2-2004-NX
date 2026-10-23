#pragma once
#include <cstdint>

// The game's rumble on the Switch's HD rumble: the Xbox controller's two motors (XInputSetState) as the low and high
// bands of a Pro Controller or Joy-Con, at the strength the debug menu sets. A rumble the game leaves on unchanged dies
// away after two seconds, and none is sent while the menu is open or the port is not in front (the HOME menu). The
// Switch Lite has no rumble of its own: Joy-Con or a Pro Controller paired with it do.
namespace sw::rumble {

// the game's request, any thread: 0 to 65535 for each motor
void Set(uint16_t left, uint16_t right);
// main thread, about 60 times a second: what is asked goes to the controllers; quiet: nothing now
void Update(bool quiet);
// at quit: the motors stopped
void Stop();

}  // namespace sw::rumble
