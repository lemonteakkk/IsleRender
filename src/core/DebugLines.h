#pragma once
#include <vector>

#include <glm/glm.hpp>

namespace isle {

struct DebugLine {
    glm::vec3 a, b;
    glm::vec3 color;
};

using DebugLineList = std::vector<DebugLine>;

inline void addCross(DebugLineList& out, const glm::vec3& p, float s, const glm::vec3& c) {
    out.push_back({p - glm::vec3(s, 0, 0), p + glm::vec3(s, 0, 0), c});
    out.push_back({p - glm::vec3(0, s, 0), p + glm::vec3(0, s, 0), c});
    out.push_back({p - glm::vec3(0, 0, s), p + glm::vec3(0, 0, s), c});
}

} // namespace isle
