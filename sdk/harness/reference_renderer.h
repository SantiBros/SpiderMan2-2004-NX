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
// 24 bit TGA of a surface's colour
bool WriteTga(const gpu::Surface& s, const char* path);
// a surface's colour as RGBA8 (red in the low byte), top row first
bool ReadRgba(const gpu::Surface& s, std::vector<uint32_t>& out, uint32_t& w, uint32_t& h);

}  // namespace reference
