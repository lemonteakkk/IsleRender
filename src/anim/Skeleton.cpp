#include "anim/Skeleton.h"

#include <glm/gtc/matrix_transform.hpp>

namespace isle {

Pose blendPoses(const Pose& a, const Pose& b, float t) {
    Pose out;
    out.local.resize(a.local.size());
    for (size_t i = 0; i < a.local.size(); ++i) out.local[i] = glm::slerp(a.local[i], b.local[i], t);
    out.hipsOffset = glm::mix(a.hipsOffset, b.hipsOffset, t);
    return out;
}

int Skeleton::add(const std::string& name, int parent, const glm::vec3& offset) {
    bones_.push_back({name, parent, offset});
    if (name == "hips") hips_ = int(bones_.size()) - 1;
    return int(bones_.size()) - 1;
}

int Skeleton::find(const std::string& name) const {
    for (size_t i = 0; i < bones_.size(); ++i)
        if (bones_[i].name == name) return int(i);
    return -1;
}

Pose Skeleton::restPose() const {
    Pose p;
    p.local.assign(bones_.size(), glm::quat(1, 0, 0, 0));
    return p;
}

void Skeleton::computeGlobals(const Pose& pose, const glm::mat4& root, std::vector<glm::mat4>& out) const {
    out.resize(bones_.size());
    for (size_t i = 0; i < bones_.size(); ++i) {
        const Bone& b = bones_[i];
        glm::vec3 t = b.offset;
        if (int(i) == hips_) t += pose.hipsOffset;
        glm::mat4 local = glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(pose.local[i]);
        out[i] = (b.parent < 0 ? root : out[size_t(b.parent)]) * local;
    }
}

void Skeleton::finalize() {
    std::vector<glm::mat4> g;
    computeGlobals(restPose(), glm::mat4(1.0f), g);
    inverseBind_.resize(g.size());
    restJoints_.resize(g.size());
    for (size_t i = 0; i < g.size(); ++i) {
        inverseBind_[i] = glm::inverse(g[i]);
        restJoints_[i] = glm::vec3(g[i][3]);
    }
}

} // namespace isle
