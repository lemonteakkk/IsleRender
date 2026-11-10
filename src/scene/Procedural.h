#pragma once
#include "gfx/Mesh.h"

#include <cstdint>

namespace isle {

// Low-poly, flat-shaded procedural props. Colours are linear-space albedo.
MeshData makePineTree();
MeshData makeRoundTree();
MeshData makeRock(uint32_t seed);
MeshData makeLantern();          // lamp part has uv.x = 1 (emissive mask)
MeshData makeGrid(int cells, float size);

} // namespace isle
