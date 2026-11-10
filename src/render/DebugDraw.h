#pragma once
#include "core/DebugLines.h"
#include "gfx/GL.h"
#include "gfx/Shader.h"

namespace isle {

// Immediate-mode line overlay (skeleton, IK targets, ...) drawn after post-processing.
class DebugDraw {
public:
    ~DebugDraw();
    bool init();
    void draw(const DebugLineList& lines);
    Shader& shader() { return shader_; }

private:
    Shader shader_;
    GLuint vao_ = 0, vbo_ = 0;
    size_t capacity_ = 0;
};

} // namespace isle
