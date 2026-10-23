#pragma once
#include <cstdint>

#include "switch/settings.h"

// The port's debug menu: hold ZL + ZR and press - (in any order) to open it, the same, B or + to close it. It
// changes the port's settings (language, the picture, vibration, Switch clocks, the frame-rate counter, sound volume,
// the renderer), saved right away to settings.ini. While it is open the game gets no input. Also draws the frame-rate counter.
namespace sw::menu {

// main thread, every 60th of a second or so: the buttons, the touch screen, the counter
void Update();
bool IsOpen();
// for the game's controller: true when it should see nothing pressed (the menu is open, or the buttons that closed
// it are still held); hide_back: - is part of the menu's combination being pressed, keep it from the game
bool Blocking(bool& hide_back);
// the language the console is set to, as the game's code (1 English, 3 German, 4 French, 5 Spanish, 6 Italian;
// 0 another one), for the menu's "Auto"
void SetConsoleLanguage(uint32_t code);
// the settings the renderer and the game read on their own threads (volume, resolution, screen, upscaling): at start,
// and from the menu as they change
void ApplyPicture(const Settings& s);

}  // namespace sw::menu
