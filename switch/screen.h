#pragma once
#include <cstdint>
#include <string>

// What the Switch shows, on one 1280x720 window for the whole run. Two ways to draw it:
// - GL (the default): an EGL window surface. The main thread draws the text screens until the game's renderer takes
//   the context (GlTake); from then on the renderer draws every frame and puts the status text over them until the
//   game draws its first picture.
// - framebuffer: libnx's CPU framebuffer, presented only by the main thread (Pump): the text screens, then the CPU
//   renderer's frames scaled to the screen's height. Used when GL cannot start, or with R held at start.
// 0.0.3 swapped libnx's text console for a framebuffer when the first (black) frame came and froze soon after, so
// there is no console any more; the text is drawn here with the console's font.
namespace sw {

// gl: try GL first
void Init(bool gl);
// how the screen is drawn, for the log: "GL: ..." or "framebuffer (...)"
const std::string& ScreenInfo();
// the text screen: a title, a block of text and a footer line
void Message(const std::string& title, const std::string& detail, const std::string& footer);
// framebuffer mode: a frame of the game, RGBA8 with red in the low byte, top row first; any thread, copied for the
// next Pump
void Frame(const uint32_t* rgba, uint32_t w, uint32_t h);
// main thread: puts the newest text or frame on screen when something changed
void Pump();
void Close();
// the game's own picture is on screen; the text screen is gone for good
bool ShowingGame();
// + and - held together for a second
bool QuitRequested();

// GL mode, for the renderer
bool UsingGl();
// the GL context moves to the calling thread (the first time from the main thread, which stops drawing). false: no GL
bool GlTake();
// the calling thread lets go of the context
void GlRelease();
// a frame went onto the window, before GlSwap: the status text over it while the game has drawn nothing
void GlPresented(uint32_t draws);
void GlSwap();
// the renderer could not start on GL: the window goes back to the framebuffer, for the CPU renderer
void GlFallBack();

}  // namespace sw
