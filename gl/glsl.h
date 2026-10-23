#pragma once
// GLSL 4.30 for the title's shaders and fixed function state: the SDK's HLSL translation (gpu/shaders.cpp) carried
// over line for line, so both renderers read the XDK's definitions the same way. Vertices come as the title made them,
// converted by the input assembler (or on the CPU when it cannot read them), constants in two uniform blocks laid out
// as gpu::DrawConstants.
#include <cstdint>
#include <string>
#include <vector>

#include "gpu/shaders.h"

namespace glsl {

// gpu::TexInfo::flags bit 0, the backend's: a movie frame, sampled through Catmull-Rom (sharper than bilinear when
// magnified). gl_backend.cpp sets it, with kTexWide for upscaled targets shrunk (spread taps) and ext's constants:
// ext.xy host pixels per console pixel on the bound target, ext.z the 16:9 picture's squeeze of what the game draws
// flat (0 off), ext.w 1 when every flat vertex is squeezed (movie frames, small draws inside the screen), 0 when those
// on the left and right edges stay there.
constexpr uint8_t kTexSharp = 1;

// a vertex program's microcode; regs: the v registers the draw supplies, bit per register; decode: Assembled::decode
std::string VertexProgram(const std::vector<uint32_t>& program, uint16_t regs, uint64_t decode, bool quad);
// D3D's fixed function vertex pipeline; pretransformed for XYZRHW vertices
std::string FixedVertex(uint16_t regs, uint64_t decode, bool pretransformed, const gpu::FixedKey& f, bool quad);
// the combiners or the fixed function stages, per the key
std::string Pixel(const gpu::PixelKey& k);

// a full screen pass's vertex shader: uv over the triangle covering the target (gl_VertexID 0 to 2)
extern const char* kFullScreenVertex;

}  // namespace glsl
