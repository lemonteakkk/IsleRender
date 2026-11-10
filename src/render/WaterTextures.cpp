#include "render/WaterTextures.h"

#include "scene/Noise.h"

#include <cmath>
#include <vector>

#include <glm/glm.hpp>

namespace isle {

namespace {

// Value noise whose lattice wraps every `period` cells, so the texture tiles seamlessly.
float periodicNoise(float x, float y, int period, uint32_t seed) {
    float fx = std::floor(x), fy = std::floor(y);
    int ix = int(fx), iy = int(fy);
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * tx * (tx * (tx * 6 - 15) + 10);
    ty = ty * ty * ty * (ty * (ty * 6 - 15) + 10);
    auto h = [&](int i, int j) { return hash01(((i % period) + period) % period, ((j % period) + period) % period, seed); };
    float a = h(ix, iy), b = h(ix + 1, iy), c = h(ix, iy + 1), d = h(ix + 1, iy + 1);
    return glm::mix(glm::mix(a, b, tx), glm::mix(c, d, tx), ty);
}

float periodicFbm(float u, float v, uint32_t seed) {
    // u, v in [0,1): base lattice of 8 cells, each octave doubles frequency (and period)
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    int period = 8;
    for (int o = 0; o < 5; ++o) {
        sum += periodicNoise(u * float(period), v * float(period), period, seed + uint32_t(o) * 31u) * amp;
        norm += amp;
        amp *= 0.5f;
        period *= 2;
    }
    return sum / norm;
}

} // namespace

GLuint createWaterDetailTexture(int size) {
    std::vector<float> height(size_t(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            height[size_t(y) * size + x] = periodicFbm(float(x) / float(size), float(y) / float(size), 4242u);

    auto H = [&](int x, int y) { return height[size_t((y + size) % size) * size + size_t((x + size) % size)]; };
    std::vector<glm::vec4> texels(size_t(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            // slopes per texel, scaled so the shader can use them like the old analytic normal
            float dx = (H(x + 1, y) - H(x - 1, y)) * 0.5f * float(size) / 64.0f;
            float dz = (H(x, y + 1) - H(x, y - 1)) * 0.5f * float(size) / 64.0f;
            texels[size_t(y) * size + x] = glm::vec4(dx, dz, H(x, y), 1.0f);
        }

    const int levels = int(std::floor(std::log2(float(size)))) + 1;
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    glTextureStorage2D(tex, levels, GL_RGBA16F, size, size);
    glTextureSubImage2D(tex, 0, 0, 0, size, size, GL_RGBA, GL_FLOAT, texels.data());
    glGenerateTextureMipmap(tex);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (GLEW_EXT_texture_filter_anisotropic) glTextureParameterf(tex, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
    return tex;
}

} // namespace isle
