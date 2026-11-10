#include "scene/Terrain.h"

#include "scene/Noise.h"

#include <algorithm>
#include <cmath>

namespace isle {

static float smoothstep(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

void Terrain::generate(int resolution, float size, uint32_t seed) {
    res_ = resolution;
    size_ = size;
    heights_.assign(size_t(res_) * res_, 0.0f);
    const float half = size * 0.5f;
    for (int j = 0; j < res_; ++j) {
        for (int i = 0; i < res_; ++i) {
            float x = -half + size * float(i) / float(res_ - 1);
            float z = -half + size * float(j) / float(res_ - 1);

            // Coastline: radial falloff perturbed by low-frequency noise.
            float d = std::sqrt(x * x + z * z) / half;
            float edge = d + (fbm(x * 0.0045f, z * 0.0045f, 4, seed + 7) - 0.5f) * 0.55f;
            float mask = 1.0f - smoothstep(0.28f, 0.78f, edge);

            float rolling = fbm(x * 0.006f, z * 0.006f, 5, seed);
            float ridge = ridgedFbm(x * 0.0085f + 13.0f, z * 0.0085f - 7.0f, 5, seed + 31);
            float mountain = smoothstep(0.35f, 0.85f, mask);
            float land = 1.5f + rolling * 12.0f + ridge * 42.0f * mountain * mountain;

            // Flatten a little band just above sea level to grow beaches.
            float beach = smoothstep(0.05f, 0.35f, mask);
            land = land * beach + 1.2f * (1.0f - beach);

            // seabed shelves off toward the tile edge so it fades into deep water seamlessly
            float seabed = -12.0f - 34.0f * smoothstep(0.7f, 1.0f, d);
            heights_[size_t(j) * res_ + i] = land * mask + seabed * (1.0f - mask);
        }
    }
}

float Terrain::sample(int i, int j) const {
    i = std::clamp(i, 0, res_ - 1);
    j = std::clamp(j, 0, res_ - 1);
    return heights_[size_t(j) * res_ + i];
}

float Terrain::heightAt(float x, float z) const {
    float fx = (x + halfSize()) / size_ * float(res_ - 1);
    float fz = (z + halfSize()) / size_ * float(res_ - 1);
    int i = int(std::floor(fx)), j = int(std::floor(fz));
    float tx = fx - float(i), tz = fz - float(j);
    float a = sample(i, j), b = sample(i + 1, j), c = sample(i, j + 1), d = sample(i + 1, j + 1);
    return (a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz;
}

glm::vec3 Terrain::normalAt(float x, float z) const {
    float e = size_ / float(res_ - 1);
    float hl = heightAt(x - e, z), hr = heightAt(x + e, z);
    float hd = heightAt(x, z - e), hu = heightAt(x, z + e);
    return glm::normalize(glm::vec3(hl - hr, 2.0f * e, hd - hu));
}

MeshData Terrain::buildMesh() const {
    MeshData m;
    m.vertices.resize(size_t(res_) * res_);
    const float half = halfSize();
    const float step = size_ / float(res_ - 1);
    for (int j = 0; j < res_; ++j) {
        for (int i = 0; i < res_; ++i) {
            Vertex& v = m.vertices[size_t(j) * res_ + i];
            float x = -half + step * float(i), z = -half + step * float(j);
            v.pos = {x, sample(i, j), z};
            float hl = sample(i - 1, j), hr = sample(i + 1, j), hd = sample(i, j - 1), hu = sample(i, j + 1);
            v.normal = glm::normalize(glm::vec3(hl - hr, 2.0f * step, hd - hu));
            v.uv = {x, z};
            v.color = glm::vec3(1.0f);
        }
    }
    m.indices.reserve(size_t(res_ - 1) * (res_ - 1) * 6);
    for (int j = 0; j < res_ - 1; ++j) {
        for (int i = 0; i < res_ - 1; ++i) {
            uint32_t a = uint32_t(j * res_ + i), b = a + 1, c = a + uint32_t(res_), d = c + 1;
            m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
        }
    }
    return m;
}

} // namespace isle
