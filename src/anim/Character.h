#pragma once
#include "anim/Clip.h"
#include "anim/Skeleton.h"
#include "core/DebugLines.h"
#include "gfx/SkinnedMesh.h"

#include <memory>
#include <vector>

namespace isle {

class Terrain;

struct CharacterInput {
    glm::vec2 move{0.0f};   // x = strafe right, y = forward (camera relative)
    bool run = false;
    bool jump = false;
    float cameraYaw = 0.0f;
};

struct CharacterTuning {
    float walkSpeed = 1.9f;
    float runSpeed = 5.6f;
    float acceleration = 14.0f;
    float turnRate = 10.0f;
    float jumpVelocity = 5.2f;
    float gravity = 18.0f;
    float walkStride = 1.45f;   // metres travelled per full walk cycle (two steps)
    float runStride = 2.7f;
    bool footIK = true;
};

// Third-person character: controller, blended keyframe locomotion, terrain foot IK
// and a GPU-skinned procedural mesh.
class Character {
public:
    static constexpr int kMaxBones = 32;

    void init();
    void spawn(const glm::vec3& position, float yaw);
    void update(float dt, const CharacterInput& in, const Terrain& terrain);

    const std::vector<glm::mat4>& skinMatrices() const { return palette_; }
    const SkinnedMesh& mesh() const { return mesh_; }
    glm::vec3 position() const { return position_; }
    float yaw() const { return yaw_; }
    float speed() const { return glm::length(glm::vec2(velocity_.x, velocity_.z)); }
    bool grounded() const { return grounded_; }
    void appendDebugLines(DebugLineList& out) const;

    CharacterTuning tuning;
    // exposed for the debug UI
    float weightIdle = 1, weightWalk = 0, weightRun = 0, weightAir = 0;
    float pelvisOffset = 0.0f;

private:
    void buildSkeleton();
    void buildClips();
    void buildMesh();
    void applyFootIK(Pose& pose, const glm::mat4& root, const Terrain& terrain, float dt);

    Skeleton skeleton_;
    Clip idle_, walk_, run_, air_;
    SkinnedMesh mesh_;

    glm::vec3 position_{0.0f};
    glm::vec3 velocity_{0.0f};
    float yaw_ = 0.0f;
    bool grounded_ = true;
    float cyclePhase_ = 0.0f, idlePhase_ = 0.0f;
    float ikWeight_ = 1.0f;

    std::vector<glm::mat4> globals_, palette_;
    glm::vec3 ikTargets_[2]{};
    int leg_[2][3]{};   // upperLeg, lowerLeg, foot (L, R)
};

} // namespace isle
