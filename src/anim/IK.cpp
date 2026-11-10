#include "anim/IK.h"

#include <algorithm>
#include <cmath>

namespace isle {

namespace {
float safeAcos(float x) { return std::acos(std::clamp(x, -1.0f, 1.0f)); }
glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    float l = glm::length(v);
    return l > 1e-5f ? v / l : fallback;
}
} // namespace

void solveTwoBoneIK(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& t,
                    const glm::quat& aGlobal, const glm::quat& bGlobal, glm::quat& aLocal, glm::quat& bLocal,
                    const glm::vec3& hingeAxis) {
    const float eps = 0.001f;
    float lab = glm::length(b - a);
    float lcb = glm::length(b - c);
    float lat = std::clamp(glm::length(t - a), eps, lab + lcb - eps);

    glm::vec3 ac = safeNormalize(c - a, glm::vec3(0, -1, 0));
    glm::vec3 ab = safeNormalize(b - a, glm::vec3(0, -1, 0));
    glm::vec3 ba = -ab;
    glm::vec3 bc = safeNormalize(c - b, glm::vec3(0, -1, 0));
    glm::vec3 at = safeNormalize(t - a, ac);

    // current and desired interior angles
    float acAb0 = safeAcos(glm::dot(ac, ab));
    float baBc0 = safeAcos(glm::dot(ba, bc));
    float acAt0 = safeAcos(glm::dot(ac, at));
    float acAb1 = safeAcos((lcb * lcb - lab * lab - lat * lat) / (-2.0f * lab * lat));
    float baBc1 = safeAcos((lat * lat - lab * lab - lcb * lcb) / (-2.0f * lab * lcb));

    glm::vec3 axis0 = safeNormalize(glm::cross(ac, ab), hingeAxis);
    glm::vec3 axis1 = safeNormalize(glm::cross(ac, at), axis0);

    glm::quat invA = glm::inverse(aGlobal), invB = glm::inverse(bGlobal);
    glm::quat r0 = glm::angleAxis(acAb1 - acAb0, invA * axis0);
    glm::quat r1 = glm::angleAxis(baBc1 - baBc0, invB * axis0);
    glm::quat r2 = glm::angleAxis(acAt0, invA * axis1);

    aLocal = aLocal * (r0 * r2);
    bLocal = bLocal * r1;
}

} // namespace isle
