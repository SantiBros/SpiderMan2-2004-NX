#pragma once
#include <cstdint>
#include <string>

// What the Switch shows. One 1280x720 framebuffer for the whole run, presented only by the main thread (Pump):
// a text screen until the game draws its first picture, then the game's frames scaled to the screen's height.
// 0.0.3 swapped libnx's text console for a framebuffer when the first (black) frame came and froze soon after,
// so there is no console any more; the text is drawn here with the console's font.
namespace sw {

void Init();
// the text screen: a title, a block of text and a footer line
void Message(const std::string& title, const std::string& detail, const std::string& footer);
// a frame of the game, RGBA8 with red in the low byte, top row first; any thread, copied for the next Pump
void Frame(const uint32_t* rgba, uint32_t w, uint32_t h);
// main thread: puts the newest text or frame on screen when something changed
void Pump();
void Close();
// a game frame was handed over; the text screen is gone for good
bool ShowingGame();
// + and - held together for a second
bool QuitRequested();

}  // namespace sw
