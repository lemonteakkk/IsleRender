#pragma once
#include "gfx/Mesh.h"

#include <cstdint>
#include <vector>

namespace isle {

// Procedural island heightfield centred on the origin. Sea level is y = 0.
class Terrain {
public:
    void generate(int resolution, float size, uint32_t seed);
    float heightAt(float x, float z) const;   // bilinear, clamped
    glm::vec3 normalAt(float x, float z) const;
    MeshData buildMesh() const;

    float size() const { return size_; }
    float halfSize() const { return size_ * 0.5f; }

private:
    float sample(int i, int j) const;
    int res_ = 0;
    float size_ = 0;
    std::vector<float> heights_;
};

} // namespace isle
