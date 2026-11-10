#pragma once
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace isle {

struct Bone {
    std::string name;
    int parent;          // -1 for the root; parents always precede children
    glm::vec3 offset;    // rest translation relative to the parent joint
};

// Local joint rotations plus a translation applied to the hips (bob / pelvis IK).
struct Pose {
    std::vector<glm::quat> local;
    glm::vec3 hipsOffset{0.0f};
};

Pose blendPoses(const Pose& a, const Pose& b, float t);  // slerp per joint

class Skeleton {
public:
    int add(const std::string& name, int parent, const glm::vec3& offset);
    void finalize();  // computes inverse bind matrices from the rest pose

    int find(const std::string& name) const;
    int size() const { return int(bones_.size()); }
    const Bone& bone(int i) const { return bones_[size_t(i)]; }
    int hips() const { return hips_; }

    Pose restPose() const;
    // World (or model) space joint matrices for a pose.
    void computeGlobals(const Pose& pose, const glm::mat4& root, std::vector<glm::mat4>& out) const;
    const std::vector<glm::mat4>& inverseBind() const { return inverseBind_; }
    const std::vector<glm::vec3>& restJoints() const { return restJoints_; }

private:
    std::vector<Bone> bones_;
    std::vector<glm::mat4> inverseBind_;
    std::vector<glm::vec3> restJoints_;
    int hips_ = -1;
};

} // namespace isle
