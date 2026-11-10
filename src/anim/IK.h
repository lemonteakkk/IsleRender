#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace isle {

// Analytic two-bone IK (law of cosines), after D. Holden's formulation.
// a/b/c: world positions of root, mid and end joints; t: target for c.
// aGlobal/bGlobal: current world rotations of a and b. aLocal/bLocal are updated in place.
// `hingeAxis` (world) is used when the chain is nearly straight and the bend plane is undefined.
void solveTwoBoneIK(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& t,
                    const glm::quat& aGlobal, const glm::quat& bGlobal, glm::quat& aLocal, glm::quat& bLocal,
                    const glm::vec3& hingeAxis);

} // namespace isle
