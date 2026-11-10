#pragma once
#include <cmath>

#include <glm/glm.hpp>

namespace isle {

// Six clip planes extracted from a view-projection matrix (Gribb & Hartmann).
// Works for perspective cameras and for the orthographic cascade matrices alike.
struct Frustum {
    glm::vec4 planes[6];  // xyz normal (pointing inside), w distance

    static Frustum fromMatrix(const glm::mat4& m) {
        Frustum f;
        glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
        glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
        glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
        glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);
        f.planes[0] = r3 + r0;
        f.planes[1] = r3 - r0;
        f.planes[2] = r3 + r1;
        f.planes[3] = r3 - r1;
        f.planes[4] = r3 + r2;
        f.planes[5] = r3 - r2;
        for (auto& p : f.planes) p /= glm::length(glm::vec3(p));
        return f;
    }

    bool intersectsAABB(const glm::vec3& mn, const glm::vec3& mx) const {
        for (const auto& p : planes) {
            // the box corner furthest along the plane normal
            glm::vec3 v(p.x >= 0 ? mx.x : mn.x, p.y >= 0 ? mx.y : mn.y, p.z >= 0 ? mx.z : mn.z);
            if (glm::dot(glm::vec3(p), v) + p.w < 0.0f) return false;
        }
        return true;
    }

    bool intersectsSphere(const glm::vec3& c, float r) const {
        for (const auto& p : planes)
            if (glm::dot(glm::vec3(p), c) + p.w < -r) return false;
        return true;
    }
};

} // namespace isle
