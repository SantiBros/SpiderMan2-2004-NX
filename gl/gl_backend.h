#pragma once
// The title's Direct3D 8 calls drawn with OpenGL 4.3 core: on the Switch's GPU through Mesa's nouveau driver, and on
// Linux through any Mesa driver for the test build. It follows the SDK's D3D12 renderer (sdk/runtime/gpu/d3d12.cpp):
// console surfaces become GL textures the draws render into, every one scaled alike when the picture is drawn bigger
// than the console's, read back to guest memory (at its size) when the CPU locks them and uploaded again when the CPU
// writes them; textures are decoded from guest memory and kept while their bytes stay the same.
#include <cstdint>
#include <memory>
#include <string>

#include "gpu/backend.h"

namespace glr {

// how the platform wants the frames drawn and shown, asked once a frame
struct Picture {
    // the game's picture drawn this many pixels tall, every surface scaled alike (720: 960x720 for 4:3); 0 or the
    // console's height: as the console draws it
    uint32_t height = 0;
    // the game's own 16:9 picture (src/game/widescreen.cpp): shown 16:9 across the screen, drawn wider when scaled,
    // what it draws flat (HUD, menus, text, movies) kept 4:3 in the middle
    bool wide = false;
    // a 4:3 picture's sides: black, the picture blurred, or a pattern
    enum class Sides : uint8_t { Black, Blur, Web } sides = Sides::Black;
    // the game's movies (YUY2 frames) and the frame onto the screen upscaled with a sharper filter than bilinear
    bool sharp_movies = false;
    // mipmapped textures read with this much anisotropic filtering (1: none), sharper at grazing angles: streets,
    // building fronts
    uint32_t anisotropy = 1;
};

// what the renderer needs from the platform; every call on the thread that draws. The renderer is set up on the
// thread that makes the device and may then be driven from another (a render thread): the context moves with it.
class Host {
public:
    virtual ~Host() = default;
    // makes the GL context current on the calling thread, made and its functions loaded the first time; false: no GL
    virtual bool MakeCurrent() = 0;
    // the calling thread lets go of the context, for another to make it current
    virtual void Release() = 0;
    // the screen the frames go to: a framebuffer object, 0 for the window's, and its size
    virtual uint32_t ScreenFramebuffer() { return 0; }
    virtual void ScreenSize(uint32_t& w, uint32_t& h) = 0;
    // after a frame went onto the screen, before Swap, with the frame's draw calls (0: only clears, the game has no
    // picture of its own yet)
    virtual void Presented(uint32_t /*draws*/) {}
    virtual void Swap() = 0;
    // frames shown every `blanks` vertical blanks from now on (gpu::Backend::SetSwapInterval)
    virtual void SetSwapInterval(uint32_t /*blanks*/) {}
    // where the list of the programs built is kept between runs, empty for nowhere
    virtual std::string CachePath() { return {}; }
    // read at the start and after every frame; a new size makes every surface again at it, contents kept
    virtual Picture GetPicture() { return {}; }
};

// null when GL can't be set up (no context, missing functions, GL older than 4.3)
std::unique_ptr<gpu::Backend> CreateBackend(Host* host);

// a w x h RGBA8 image (red in the low byte, top row first) over the whole bound framebuffer; for the platform's own
// screens. upload false draws the image of the last call again. Made on first use, on the GL thread.
void DrawImage(const uint32_t* rgba, uint32_t w, uint32_t h, uint32_t screen_w, uint32_t screen_h, bool upload = true);
// a w x h RGBA8 image blended by its alpha over the bound framebuffer at x, y (pixels from the top left), for the
// platform's overlays; each slot (0 to 3) keeps its own texture, upload false draws its last image again
void DrawOverlay(int slot, const uint32_t* rgba, uint32_t w, uint32_t h, int x, int y, uint32_t screen_w,
                 uint32_t screen_h, bool upload = true);

}  // namespace glr
