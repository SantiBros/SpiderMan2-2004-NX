#pragma once
#include <cstdint>
#include <string>
#include <vector>

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
// the start screen: a title and one line in the middle, a footer line
void Loading(const std::string& title, const std::string& line, const std::string& footer);
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

// An RGBA picture drawn on the CPU (red in the low byte, alpha in the top one), with the console's font: 16 x 16
// pixels a character, times `scale`. For the overlays.
struct Canvas {
    uint32_t w = 0, h = 0;
    std::vector<uint32_t> px;
    void Reset(uint32_t width, uint32_t height, uint32_t color);
    // filled; the colour replaces what is there, alpha too
    void Rect(int x0, int y0, int x1, int y1, uint32_t color);
    // one line with its top left at x, y, cut at the edge; returns the x after it
    int Text(int x, int y, const std::string& s, uint32_t color, int scale = 1);
};
int TextWidth(const std::string& s, int scale = 1);

// over the game's frames and the text screen, blended by its alpha, at x, y on the 1280x720 screen: layer 0 the
// frame-rate counter, 1 the debug menu. Any thread; an empty canvas takes the layer away.
constexpr int kLayers = 2;
void SetOverlay(int layer, const Canvas& c, int x, int y);

// GL mode, for the renderer
bool UsingGl();
// the GL context moves to the calling thread (the first time from the main thread, which stops drawing). false: no GL
bool GlTake();
// the calling thread lets go of the context
void GlRelease();
// a frame went onto the window, before GlSwap: the status text over it while the game has drawn nothing
void GlPresented(uint32_t draws);
void GlSwap();
// a frame every `blanks` vertical blanks, 0 at once (the game's presentation interval)
void GlSwapInterval(uint32_t blanks);
// the renderer could not start on GL: the window goes back to the framebuffer, for the CPU renderer
void GlFallBack();

}  // namespace sw
