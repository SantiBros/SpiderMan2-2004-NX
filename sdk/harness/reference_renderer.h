#pragma once
#include <cstdint>
#include <vector>

#include "gpu/state.h"

// CPU renderer for the headless harness: draws the title's calls into its own surfaces in guest memory, so a frame
// can be looked at without a GPU. Follows the same decoding of the XDK state as the SDK's shader translator.
namespace reference {

void BeginFrame();  // drops decoded textures and restarts the draw count
void Clear(const gpu::State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil);
void Copy(const gpu::Surface& src, const gpu::Surface& dst, uint32_t count, uint32_t rects_va, uint32_t points_va);
void Draw(const gpu::State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices);
// what the last Draw did with its triangles and pixels, for the draw lists
struct DrawStats {
    uint32_t draw = 0;  // its number in the frame
    uint32_t triangles = 0, clipped = 0, culled = 0, outside = 0, pixels = 0;
    uint32_t killed = 0, alpha_failed = 0, stencil_failed = 0, z_failed = 0, written = 0;
    bool skipped = false;  // not drawn at all: no triangles assembled, or no target
    float min_x = 0, min_y = 0, max_x = 0, max_y = 0;  // screen box of the triangles before clipping to the viewport
};
const DrawStats& LastDrawStats();
// visibility tests: pixels that passed depth and stencil in draws made while the title counted (kMCountEnable)
uint64_t CountedPixels();
void ResetCount();
// IDMAP=1: the numbers of the last four draws that wrote each pixel of a target, newest first, 0xFFFF for none
const std::vector<uint16_t>* DrawIds(const gpu::Surface& s, uint32_t& w, uint32_t& h);
// 24 bit TGA of a surface's colour
bool WriteTga(const gpu::Surface& s, const char* path);
// a surface's colour as RGBA8 (red in the low byte), top row first
bool ReadRgba(const gpu::Surface& s, std::vector<uint32_t>& out, uint32_t& w, uint32_t& h);

}  // namespace reference
