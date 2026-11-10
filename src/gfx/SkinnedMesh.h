#pragma once
#include "gfx/GL.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace isle {

struct SkinVertex {
    glm::vec3 pos{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 color{1.0f};
    glm::vec4 joints{0.0f};  // joint indices stored as floats: integer DSA attribs (IFormat) are broken on some Intel drivers
    glm::vec4 weights{1, 0, 0, 0};
};

struct SkinMeshData {
    std::vector<SkinVertex> vertices;
    std::vector<uint32_t> indices;
};

// Mesh skinned on the GPU: locations 0 pos, 1 normal, 3 color, 5 joints (float-encoded), 6 weights.
class SkinnedMesh {
public:
    SkinnedMesh() = default;
    ~SkinnedMesh();
    SkinnedMesh(const SkinnedMesh&) = delete;
    SkinnedMesh& operator=(const SkinnedMesh&) = delete;

    void upload(const SkinMeshData& data);
    void draw() const;
    GLsizei indexCount() const { return indexCount_; }

private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    GLsizei indexCount_ = 0;
};

} // namespace isle
