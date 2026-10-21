#pragma once
#include <cstdint>
#include <string>

// What the Switch shows: libnx's text console for messages before the game draws, then a 1280x720 framebuffer the
// game's frames are scaled into (640x480 at 1.5x, centred).
namespace sw {

void Message(const std::string& title, const std::string& detail, const std::string& footer);
void CloseText();
// a frame of the game, RGBA8 with red in the low byte, top row first
void Frame(const uint32_t* rgba, uint32_t w, uint32_t h);
void Close();
// the game's frames are on screen (the text console is gone)
bool ShowingGame();
// + and - held together for a second
bool QuitRequested();

}  // namespace sw
