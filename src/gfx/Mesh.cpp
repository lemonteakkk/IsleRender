#include "gfx/Mesh.h"

#include <cstddef>

namespace isle {

Mesh::~Mesh() { release(); }

void Mesh::release() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    GLuint bufs[] = {vbo_, ebo_, instanceVbo_};
    glDeleteBuffers(3, bufs);
    vao_ = vbo_ = ebo_ = instanceVbo_ = 0;
    indexCount_ = instanceCount_ = 0;
}

void Mesh::upload(const MeshData& data) {
    release();
    glCreateVertexArrays(1, &vao_);
    glCreateBuffers(1, &vbo_);
    glCreateBuffers(1, &ebo_);
    glNamedBufferStorage(vbo_, GLsizeiptr(data.vertices.size() * sizeof(Vertex)), data.vertices.data(), 0);
    glNamedBufferStorage(ebo_, GLsizeiptr(data.indices.size() * sizeof(uint32_t)), data.indices.data(), 0);

    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(vao_, ebo_);
    struct Attr { GLuint loc; GLint n; GLuint off; };
    const Attr attrs[] = {{0, 3, offsetof(Vertex, pos)},
                          {1, 3, offsetof(Vertex, normal)},
                          {2, 2, offsetof(Vertex, uv)},
                          {3, 3, offsetof(Vertex, color)}};
    for (const auto& a : attrs) {
        glEnableVertexArrayAttrib(vao_, a.loc);
        glVertexArrayAttribFormat(vao_, a.loc, a.n, GL_FLOAT, GL_FALSE, a.off);
        glVertexArrayAttribBinding(vao_, a.loc, 0);
    }
    indexCount_ = GLsizei(data.indices.size());
}

void Mesh::setInstances(const std::vector<glm::mat4>& transforms) {
    if (!instanceVbo_) {
        glCreateBuffers(1, &instanceVbo_);
        glVertexArrayVertexBuffer(vao_, 1, instanceVbo_, 0, sizeof(glm::mat4));
        glVertexArrayBindingDivisor(vao_, 1, 1);
        for (GLuint c = 0; c < 4; ++c) {
            glEnableVertexArrayAttrib(vao_, 4 + c);
            glVertexArrayAttribFormat(vao_, 4 + c, 4, GL_FLOAT, GL_FALSE, c * sizeof(glm::vec4));
            glVertexArrayAttribBinding(vao_, 4 + c, 1);
        }
    }
    glNamedBufferData(instanceVbo_, GLsizeiptr(transforms.size() * sizeof(glm::mat4)), transforms.data(),
                      GL_STATIC_DRAW);
    instanceCount_ = GLsizei(transforms.size());
}

void Mesh::draw() const {
    if (!vao_ || indexCount_ == 0) return;
    glBindVertexArray(vao_);
    if (instanceVbo_)
        glDrawElementsInstanced(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr, instanceCount_);
    else
        glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}

void Mesh::bindInstanceBuffer(GLuint buffer) {
    glVertexArrayVertexBuffer(vao_, 1, buffer, 0, sizeof(glm::mat4));
    glVertexArrayBindingDivisor(vao_, 1, 1);
    for (GLuint c = 0; c < 4; ++c) {
        glEnableVertexArrayAttrib(vao_, 4 + c);
        glVertexArrayAttribFormat(vao_, 4 + c, 4, GL_FLOAT, GL_FALSE, c * sizeof(glm::vec4));
        glVertexArrayAttribBinding(vao_, 4 + c, 1);
    }
}

void Mesh::drawIndirect(GLuint commandBuffer, size_t byteOffset, GLsizei drawCount) const {
    if (!vao_ || drawCount == 0) return;
    glBindVertexArray(vao_);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, commandBuffer);
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, reinterpret_cast<const void*>(byteOffset), drawCount, 0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

void Mesh::drawRange(GLuint minVertex, GLuint maxVertex, GLsizei count, const void* byteOffset) const {
    glBindVertexArray(vao_);
    glDrawRangeElements(GL_TRIANGLES, minVertex, maxVertex, count, GL_UNSIGNED_INT, byteOffset);
}

void Mesh::drawInstancedRange(uint32_t firstIndex, uint32_t indexCount, GLsizei instances, GLuint baseInstance) const {
    glBindVertexArray(vao_);
    glDrawElementsInstancedBaseInstance(GL_TRIANGLES, GLsizei(indexCount), GL_UNSIGNED_INT,
                                        reinterpret_cast<const void*>(uintptr_t(firstIndex) * sizeof(uint32_t)),
                                        instances, baseInstance);
}

void Mesh::drawMulti(const GLsizei* counts, const void* const* offsets, GLsizei drawCount) const {
    if (!vao_ || drawCount == 0) return;
    glBindVertexArray(vao_);
    glMultiDrawElements(GL_TRIANGLES, counts, GL_UNSIGNED_INT, offsets, drawCount);
}

} // namespace isle
