#pragma once
#include "gfx/GL.h"

namespace isle {

// Tileable detail texture for the water surface, generated once at start-up:
//   RG = slope of a periodic fBm height field (dh/dx, dh/dz), B = height (foam mask), A = 1.
// Replaces ~28 value-noise evaluations per water pixel with two filtered texture fetches.
GLuint createWaterDetailTexture(int size = 256);

} // namespace isle
