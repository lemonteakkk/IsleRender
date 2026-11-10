#pragma once
#include "gfx/GL.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace isle {

struct Vertex {
    glm::vec3 pos{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};   // uv.x doubles as an emissive mask on procedural props
    glm::vec3 color{1.0f};
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// GPU mesh built with OpenGL 4.5 direct state access.
// Optional per-instance mat4 stream (attribute locations 4..7).
class Mesh {
public:
    Mesh() = default;
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void upload(const MeshData& data);
    void setInstances(const std::vector<glm::mat4>& transforms);
    void draw() const;
    // Several index ranges in one call (glMultiDrawElements); offsets are byte offsets.
    void drawMulti(const GLsizei* counts, const void* const* offsets, GLsizei drawCount) const;
    // Per-instance mat4 stream sourced from an external buffer (e.g. GPU culling output);
    // instances are addressed through baseInstance of indirect commands.
    void bindInstanceBuffer(GLuint buffer);
    void drawIndirect(GLuint commandBuffer, size_t byteOffset, GLsizei drawCount) const;
    void drawRange(GLuint minVertex, GLuint maxVertex, GLsizei count, const void* byteOffset) const;
    void drawInstancedRange(uint32_t firstIndex, uint32_t indexCount, GLsizei instances, GLuint baseInstance) const;

    GLsizei indexCount() const { return indexCount_; }
    GLsizei instanceCount() const { return instanceCount_; }
    bool instanced() const { return instanceCount_ > 0; }

private:
    void release();
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0, instanceVbo_ = 0;
    GLsizei indexCount_ = 0, instanceCount_ = 0;
};

} // namespace isle
