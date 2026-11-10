#pragma once
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace isle {

// Orbit camera around a target point (third-person style).
struct Camera {
    glm::vec3 target{0.0f, 8.0f, 0.0f};
    float yaw = 0.8f;       // radians, around +Y
    float pitch = 0.32f;    // radians, positive = looking down
    float distance = 90.0f;
    float fovY = glm::radians(55.0f);
    float nearPlane = 0.1f;
    float farPlane = 2500.0f;

    glm::vec3 forward() const {  // from camera toward target
        return -glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
    }
    glm::vec3 position() const { return target - forward() * distance; }
    glm::mat4 view() const { return glm::lookAt(position(), target, glm::vec3(0, 1, 0)); }
    glm::mat4 projection(float aspect) const { return glm::perspective(fovY, aspect, nearPlane, farPlane); }
};

} // namespace isle
