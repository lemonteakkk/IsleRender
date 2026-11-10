#include "gfx/Shader.h"

#include "core/Paths.h"

#include <cstdio>
#include <set>
#include <sstream>

#include <glm/gtc/type_ptr.hpp>

namespace fs = std::filesystem;

namespace isle {

static bool preprocess(const fs::path& file, std::string& out, std::set<fs::path>& seen,
                       std::vector<fs::path>& deps) {
    std::string src;
    if (!readTextFile(file, src)) {
        std::fprintf(stderr, "[shader] cannot read %s\n", file.u8string().c_str());
        return false;
    }
    deps.push_back(file);
    std::istringstream in(src);
    std::string line;
    while (std::getline(in, line)) {
        auto pos = line.find("#include");
        if (pos != std::string::npos && line.find_first_not_of(" \t") == pos) {
            auto a = line.find('"'), b = line.rfind('"');
            if (a == std::string::npos || b <= a) return false;
            fs::path inc = file.parent_path() / fs::u8path(line.substr(a + 1, b - a - 1));
            if (seen.insert(inc).second && !preprocess(inc, out, seen, deps)) return false;
            continue;
        }
        out += line;
        out += '\n';
    }
    return true;
}

static GLuint compileStage(GLenum type, const std::string& src, const std::string& name) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[shader] compile error in %s:\n%s\n", name.c_str(), log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

Shader::~Shader() {
    if (program_) glDeleteProgram(program_);
}

bool Shader::load(const std::string& vertex, const std::string& fragment,
                  const std::vector<std::string>& defines) {
    vsName_ = vertex;
    fsName_ = fragment;
    defines_ = defines;
    return build();
}

bool Shader::loadCompute(const std::string& compute, const std::vector<std::string>& defines) {
    vsName_ = compute;
    fsName_.clear();
    defines_ = defines;
    compute_ = true;
    return build();
}

bool Shader::build() {
    std::string header = "#version 450 core\n";
    for (const auto& d : defines_) header += "#define " + d + "\n";

    std::vector<fs::path> deps;
    GLuint prog = 0;
    if (compute_) {
        std::string csSrc = header;
        std::set<fs::path> seen;
        if (!preprocess(assetPath("shaders/" + vsName_), csSrc, seen, deps)) return false;
        GLuint cs = compileStage(GL_COMPUTE_SHADER, csSrc, vsName_);
        if (!cs) return false;
        prog = glCreateProgram();
        glAttachShader(prog, cs);
        glLinkProgram(prog);
        glDeleteShader(cs);
    } else {
        std::string vsSrc = header, fsSrc = header;
        std::set<fs::path> seenV, seenF;
        if (!preprocess(assetPath("shaders/" + vsName_), vsSrc, seenV, deps)) return false;
        if (!preprocess(assetPath("shaders/" + fsName_), fsSrc, seenF, deps)) return false;

        GLuint vs = compileStage(GL_VERTEX_SHADER, vsSrc, vsName_);
        GLuint fsh = compileStage(GL_FRAGMENT_SHADER, fsSrc, fsName_);
        if (!vs || !fsh) {
            if (vs) glDeleteShader(vs);
            if (fsh) glDeleteShader(fsh);
            return false;
        }
        prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fsh);
        glLinkProgram(prog);
        glDeleteShader(vs);
        glDeleteShader(fsh);
    }
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[shader] link error %s + %s:\n%s\n", vsName_.c_str(), fsName_.c_str(), log);
        glDeleteProgram(prog);
        return false;
    }
    // Keep the old program alive until the new one is valid (hot-reload safety).
    if (program_) glDeleteProgram(program_);
    program_ = prog;
    locations_.clear();
    deps_ = std::move(deps);
    stamp_ = {};
    for (const auto& d : deps_) {
        std::error_code ec;
        auto t = fs::last_write_time(d, ec);
        if (!ec && t > stamp_) stamp_ = t;
    }
    return true;
}

bool Shader::reload() { return build(); }

bool Shader::reloadIfChanged() {
    for (const auto& d : deps_) {
        std::error_code ec;
        auto t = fs::last_write_time(d, ec);
        if (!ec && t > stamp_) {
            bool ok = build();
            if (!ok) stamp_ = t;  // don't spam errors every frame
            return ok;
        }
    }
    return false;
}

GLint Shader::location(const char* name) {
    auto it = locations_.find(name);
    if (it != locations_.end()) return it->second;
    GLint loc = glGetUniformLocation(program_, name);
    locations_.emplace(name, loc);
    return loc;
}

void Shader::set(const char* n, int v) { glProgramUniform1i(program_, location(n), v); }
void Shader::set(const char* n, float v) { glProgramUniform1f(program_, location(n), v); }
void Shader::set(const char* n, const glm::vec2& v) { glProgramUniform2fv(program_, location(n), 1, glm::value_ptr(v)); }
void Shader::set(const char* n, const glm::vec3& v) { glProgramUniform3fv(program_, location(n), 1, glm::value_ptr(v)); }
void Shader::set(const char* n, const glm::vec4& v) { glProgramUniform4fv(program_, location(n), 1, glm::value_ptr(v)); }
void Shader::set(const char* n, const glm::mat4& v) {
    glProgramUniformMatrix4fv(program_, location(n), 1, GL_FALSE, glm::value_ptr(v));
}
void Shader::setArray(const char* n, const glm::vec3* v, int count) {
    glProgramUniform3fv(program_, location(n), count, glm::value_ptr(v[0]));
}
void Shader::setArray(const char* n, const glm::vec4* v, int count) {
    glProgramUniform4fv(program_, location(n), count, glm::value_ptr(v[0]));
}
void Shader::setUint(const char* n, unsigned v) { glProgramUniform1ui(program_, location(n), v); }

} // namespace isle
