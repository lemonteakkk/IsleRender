#include "render/DebugDraw.h"

#include <vector>

namespace isle {

DebugDraw::~DebugDraw() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
}

bool DebugDraw::init() {
    if (!shader_.load("debug_line.vert", "debug_line.frag")) return false;
    glCreateVertexArrays(1, &vao_);
    glEnableVertexArrayAttrib(vao_, 0);
    glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(vao_, 0, 0);
    glEnableVertexArrayAttrib(vao_, 1);
    glVertexArrayAttribFormat(vao_, 1, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3));
    glVertexArrayAttribBinding(vao_, 1, 0);
    return true;
}

void DebugDraw::draw(const DebugLineList& lines) {
    if (lines.empty()) return;
    std::vector<glm::vec3> data;
    data.reserve(lines.size() * 4);
    for (const auto& l : lines) {
        data.push_back(l.a);
        data.push_back(l.color);
        data.push_back(l.b);
        data.push_back(l.color);
    }
    size_t bytes = data.size() * sizeof(glm::vec3);
    if (bytes > capacity_) {
        if (vbo_) glDeleteBuffers(1, &vbo_);
        glCreateBuffers(1, &vbo_);
        capacity_ = bytes * 2;
        glNamedBufferStorage(vbo_, GLsizeiptr(capacity_), nullptr, GL_DYNAMIC_STORAGE_BIT);
        glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, 2 * sizeof(glm::vec3));
    }
    glNamedBufferSubData(vbo_, 0, GLsizeiptr(bytes), data.data());
    shader_.use();
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, GLsizei(lines.size() * 2));
}

} // namespace isle
