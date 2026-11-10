#include "gfx/SkinnedMesh.h"

#include <cstddef>

namespace isle {

SkinnedMesh::~SkinnedMesh() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    GLuint bufs[] = {vbo_, ebo_};
    glDeleteBuffers(2, bufs);
}

void SkinnedMesh::upload(const SkinMeshData& d) {
    glCreateVertexArrays(1, &vao_);
    glCreateBuffers(1, &vbo_);
    glCreateBuffers(1, &ebo_);
    glNamedBufferStorage(vbo_, GLsizeiptr(d.vertices.size() * sizeof(SkinVertex)), d.vertices.data(), 0);
    glNamedBufferStorage(ebo_, GLsizeiptr(d.indices.size() * sizeof(uint32_t)), d.indices.data(), 0);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(SkinVertex));
    glVertexArrayElementBuffer(vao_, ebo_);

    auto floatAttr = [&](GLuint loc, GLint n, GLuint off) {
        glEnableVertexArrayAttrib(vao_, loc);
        glVertexArrayAttribFormat(vao_, loc, n, GL_FLOAT, GL_FALSE, off);
        glVertexArrayAttribBinding(vao_, loc, 0);
    };
    floatAttr(0, 3, offsetof(SkinVertex, pos));
    floatAttr(1, 3, offsetof(SkinVertex, normal));
    floatAttr(3, 3, offsetof(SkinVertex, color));
    floatAttr(5, 4, offsetof(SkinVertex, joints));
    floatAttr(6, 4, offsetof(SkinVertex, weights));
    indexCount_ = GLsizei(d.indices.size());
}

void SkinnedMesh::draw() const {
    if (!vao_) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}

} // namespace isle
