#include "anim/Clip.h"

#include <cmath>
#include <cstdio>

namespace isle {

namespace {

template <typename T>
T catmullRom(const T& p0, const T& p1, const T& p2, const T& p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

} // namespace

Pose Clip::sample(float phase) const {
    Pose pose;
    const size_t n = keys.size();
    const size_t joints = keys.front().euler.size();
    pose.local.resize(joints);
    if (n == 1) {
        for (size_t j = 0; j < joints; ++j) pose.local[j] = glm::quat(glm::radians(keys[0].euler[j]));
        pose.hipsOffset = keys[0].hips;
        return pose;
    }
    phase -= std::floor(phase);
    // find segment [i, i+1) (cyclic)
    size_t i = n - 1;
    for (size_t k = 0; k < n; ++k) {
        float next = (k + 1 < n) ? keys[k + 1].phase : 1.0f + keys[0].phase;
        if (phase >= keys[k].phase && phase < next) { i = k; break; }
    }
    if (phase < keys[0].phase) i = n - 1;
    size_t i0 = (i + n - 1) % n, i2 = (i + 1) % n, i3 = (i + 2) % n;
    float start = keys[i].phase;
    float end = (i + 1 < n) ? keys[i + 1].phase : 1.0f + keys[0].phase;
    float p = phase < start ? phase + 1.0f : phase;
    float t = (p - start) / (end - start);

    for (size_t j = 0; j < joints; ++j) {
        glm::vec3 e = catmullRom(keys[i0].euler[j], keys[i].euler[j], keys[i2].euler[j], keys[i3].euler[j], t);
        pose.local[j] = glm::quat(glm::radians(e));
    }
    pose.hipsOffset = catmullRom(keys[i0].hips, keys[i].hips, keys[i2].hips, keys[i3].hips, t);
    return pose;
}

ClipBuilder& ClipBuilder::key(float phase, glm::vec3 hips) {
    Clip::Key k;
    k.phase = phase;
    k.euler.assign(size_t(sk_.size()), glm::vec3(0.0f));
    k.hips = hips;
    clip_.keys.push_back(k);
    return *this;
}

ClipBuilder& ClipBuilder::rot(const char* bone, glm::vec3 e) {
    int i = sk_.find(bone);
    if (i < 0) {
        std::fprintf(stderr, "[anim] unknown bone '%s' in clip %s\n", bone, clip_.name.c_str());
        return *this;
    }
    clip_.keys.back().euler[size_t(i)] = e;
    return *this;
}

} // namespace isle
