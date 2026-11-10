#pragma once
#include "anim/Skeleton.h"

#include <string>
#include <vector>

namespace isle {

// Keyframed clip over a normalised phase [0,1). Keys store per-joint Euler angles
// (degrees) so they are easy to author; sampling uses cyclic Catmull-Rom splines,
// which gives smooth motion from only a handful of keys.
class Clip {
public:
    struct Key {
        float phase;
        std::vector<glm::vec3> euler;  // per joint, degrees
        glm::vec3 hips{0.0f};
    };

    std::string name;
    std::vector<Key> keys;  // sorted by phase

    Pose sample(float phase) const;
};

class ClipBuilder {
public:
    explicit ClipBuilder(const Skeleton& sk, const char* name) : sk_(sk) { clip_.name = name; }
    // Starts a new key; subsequent rot() calls apply to it.
    ClipBuilder& key(float phase, glm::vec3 hips = glm::vec3(0.0f));
    ClipBuilder& rot(const char* bone, glm::vec3 eulerDegrees);
    Clip build() { return clip_; }

private:
    const Skeleton& sk_;
    Clip clip_;
};

} // namespace isle
