#pragma once
#include "gfx/GL.h"

namespace isle {

GLuint createTexture2D(GLenum internalFormat, int w, int h, int levels = 1, GLenum minFilter = GL_LINEAR,
                       GLenum magFilter = GL_LINEAR, GLenum wrap = GL_CLAMP_TO_EDGE);
GLuint createFramebuffer(const GLuint* colors, int colorCount, GLuint depth);
bool checkFramebuffer(GLuint fbo, const char* name);
void deleteTexture(GLuint& tex);
void deleteFramebuffer(GLuint& fbo);
void installDebugCallback();

} // namespace isle
