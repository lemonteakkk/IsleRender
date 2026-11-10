#pragma once
#include <cmath>
#include <cstdint>

namespace isle {

inline uint32_t hashU32(int32_t x, int32_t y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

inline float hash01(int32_t x, int32_t y, uint32_t seed) { return float(hashU32(x, y, seed) & 0xFFFFFFu) / 16777215.0f; }

// Smooth value noise in [0,1] with quintic interpolation.
inline float valueNoise(float x, float y, uint32_t seed) {
    float fx = std::floor(x), fy = std::floor(y);
    int32_t ix = int32_t(fx), iy = int32_t(fy);
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * tx * (tx * (tx * 6 - 15) + 10);
    ty = ty * ty * ty * (ty * (ty * 6 - 15) + 10);
    float a = hash01(ix, iy, seed), b = hash01(ix + 1, iy, seed);
    float c = hash01(ix, iy + 1, seed), d = hash01(ix + 1, iy + 1, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

inline float fbm(float x, float y, int octaves, uint32_t seed, float lacunarity = 2.0f, float gain = 0.5f) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise(x, y, seed + uint32_t(i) * 101u) * amp;
        norm += amp;
        // rotate domain a little each octave to hide grid artifacts
        float nx = x * 1.6f - y * 1.2f, ny = x * 1.2f + y * 1.6f;
        x = nx * (lacunarity / 2.0f);
        y = ny * (lacunarity / 2.0f);
        amp *= gain;
    }
    return sum / norm;
}

inline float ridgedFbm(float x, float y, int octaves, uint32_t seed) {
    float n = fbm(x, y, octaves, seed);
    float r = 1.0f - std::fabs(n * 2.0f - 1.0f);
    return r * r;
}

} // namespace isle
