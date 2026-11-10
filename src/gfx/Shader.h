#pragma once
#include "gfx/GL.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace isle {

// GLSL program built from files under assets/shaders.
// Supports `#include "file.glsl"`, per-variant #defines and hot reload on file change.
class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    bool load(const std::string& vertex, const std::string& fragment,
              const std::vector<std::string>& defines = {});
    bool loadCompute(const std::string& compute, const std::vector<std::string>& defines = {});
    bool reloadIfChanged();  // returns true if a rebuild happened
    bool reload();

    void use() const { glUseProgram(program_); }
    GLuint id() const { return program_; }

    void set(const char* name, int v);
    void set(const char* name, float v);
    void set(const char* name, const glm::vec2& v);
    void set(const char* name, const glm::vec3& v);
    void set(const char* name, const glm::vec4& v);
    void set(const char* name, const glm::mat4& v);
    void setArray(const char* name, const glm::vec3* v, int count);
    void setArray(const char* name, const glm::vec4* v, int count);
    void setUint(const char* name, unsigned v);

private:
    GLint location(const char* name);
    bool build();

    GLuint program_ = 0;
    bool compute_ = false;
    std::string vsName_, fsName_;
    std::vector<std::string> defines_;
    std::vector<std::filesystem::path> deps_;
    std::filesystem::file_time_type stamp_{};
    std::unordered_map<std::string, GLint> locations_;
};

} // namespace isle
