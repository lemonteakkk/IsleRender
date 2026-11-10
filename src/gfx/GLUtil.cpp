#include "gfx/GLUtil.h"

#include <cstdio>

namespace isle {

GLuint createTexture2D(GLenum fmt, int w, int h, int levels, GLenum minF, GLenum magF, GLenum wrap) {
    GLuint t = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &t);
    glTextureStorage2D(t, levels, fmt, w, h);
    glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GLint(minF));
    glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GLint(magF));
    glTextureParameteri(t, GL_TEXTURE_WRAP_S, GLint(wrap));
    glTextureParameteri(t, GL_TEXTURE_WRAP_T, GLint(wrap));
    return t;
}

GLuint createFramebuffer(const GLuint* colors, int colorCount, GLuint depth) {
    GLuint fbo = 0;
    glCreateFramebuffers(1, &fbo);
    GLenum bufs[8];
    for (int i = 0; i < colorCount; ++i) {
        glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0 + i, colors[i], 0);
        bufs[i] = GL_COLOR_ATTACHMENT0 + i;
    }
    if (colorCount > 0)
        glNamedFramebufferDrawBuffers(fbo, colorCount, bufs);
    else
        glNamedFramebufferDrawBuffer(fbo, GL_NONE);
    if (depth) glNamedFramebufferTexture(fbo, GL_DEPTH_ATTACHMENT, depth, 0);
    return fbo;
}

bool checkFramebuffer(GLuint fbo, const char* name) {
    GLenum s = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER);
    if (s != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "[gl] framebuffer '%s' incomplete: 0x%x\n", name, s);
        return false;
    }
    return true;
}

void deleteTexture(GLuint& t) {
    if (t) glDeleteTextures(1, &t);
    t = 0;
}

void deleteFramebuffer(GLuint& f) {
    if (f) glDeleteFramebuffers(1, &f);
    f = 0;
}

static void GLAPIENTRY debugCallback(GLenum, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* msg,
                                   const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    std::fprintf(stderr, "[gl] %s (type 0x%x id %u): %s\n",
                 severity == GL_DEBUG_SEVERITY_HIGH ? "ERROR" : "warning", type, id, msg);
}

void installDebugCallback() {
    if (!GLEW_KHR_debug && !GLEW_VERSION_4_3) return;
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(debugCallback, nullptr);
}

} // namespace isle
