#include "anim/Character.h"

#include "anim/IK.h"
#include "scene/Terrain.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace isle {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kAnkleHeight = 0.06f;

glm::vec3 srgb(float r, float g, float b) { return glm::pow(glm::vec3(r, g, b), glm::vec3(2.2f)); }

float wrapAngle(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

float approach(float current, float target, float rate, float dt) {
    return current + (target - current) * (1.0f - std::exp(-rate * dt));
}

glm::quat rotationBetween(const glm::vec3& u, const glm::vec3& v) {
    float d = glm::dot(u, v);
    glm::vec3 axis = glm::cross(u, v);
    return glm::normalize(glm::quat(1.0f + d, axis.x, axis.y, axis.z));
}

// ---- skinned mesh primitives -------------------------------------------------

SkinVertex sv(glm::vec3 p, glm::vec3 n, glm::vec3 c, int b0, int b1 = 0, float w1 = 0.0f) {
    SkinVertex v;
    v.pos = p;
    v.normal = n;
    v.color = c;
    v.joints = glm::vec4(float(b0), float(b1), 0.0f, 0.0f);
    v.weights = glm::vec4(1.0f - w1, w1, 0, 0);
    return v;
}

// Tapered (optionally elliptical) tube from a to b, rigidly bound to `bone` except near
// its start, where weights blend toward `parent` so the joint bends smoothly (LBS).
void addLimb(SkinMeshData& m, int bone, int parent, glm::vec3 a, glm::vec3 b, float ra, float rb, glm::vec3 color,
             float blend, glm::vec2 ellipse = {1, 1}, int sides = 10, int rings = 6) {
    glm::vec3 dir = glm::normalize(b - a);
    glm::vec3 ref = std::fabs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
    glm::vec3 u = glm::normalize(glm::cross(dir, ref));
    glm::vec3 w = glm::cross(dir, u);
    // order u/w so that ellipse.x scales the world-X-ish axis
    if (std::fabs(u.x) < std::fabs(w.x)) {
        std::swap(u, w);
        w = -w;  // keep (u, w, dir) right-handed so triangle winding stays outward
    }

    auto weightAt = [&](float t) { return (parent >= 0 && t < blend) ? 0.5f * (1.0f - t / blend) : 0.0f; };
    uint32_t base = uint32_t(m.vertices.size());
    for (int r = 0; r <= rings; ++r) {
        float t = float(r) / float(rings);
        glm::vec3 c = glm::mix(a, b, t);
        float rad = glm::mix(ra, rb, t);
        for (int s = 0; s < sides; ++s) {
            float ang = 2 * kPi * float(s) / float(sides);
            glm::vec3 off = (u * std::cos(ang) * ellipse.x + w * std::sin(ang) * ellipse.y) * rad;
            glm::vec3 n = glm::normalize(u * std::cos(ang) / ellipse.x + w * std::sin(ang) / ellipse.y);
            m.vertices.push_back(sv(c + off, n, color, bone, std::max(parent, 0), weightAt(t)));
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < sides; ++s) {
            uint32_t i0 = base + uint32_t(r * sides + s), i1 = base + uint32_t(r * sides + (s + 1) % sides);
            uint32_t i2 = i0 + uint32_t(sides), i3 = i1 + uint32_t(sides);
            m.indices.insert(m.indices.end(), {i0, i1, i2, i1, i3, i2});
        }
    // end caps
    for (int end = 0; end < 2; ++end) {
        float t = float(end);
        uint32_t center = uint32_t(m.vertices.size());
        m.vertices.push_back(sv(end ? b : a, end ? dir : -dir, color, bone, std::max(parent, 0), weightAt(t)));
        uint32_t ring = base + uint32_t(end * rings * sides);
        for (int s = 0; s < sides; ++s) {
            uint32_t i0 = ring + uint32_t(s), i1 = ring + uint32_t((s + 1) % sides);
            if (end) m.indices.insert(m.indices.end(), {center, i0, i1});
            else m.indices.insert(m.indices.end(), {center, i1, i0});
        }
    }
}

void addEllipsoid(SkinMeshData& m, int bone, glm::vec3 c, glm::vec3 r, glm::vec3 color, int lat = 10, int lon = 14) {
    uint32_t base = uint32_t(m.vertices.size());
    for (int i = 0; i <= lat; ++i) {
        float th = kPi * float(i) / float(lat);
        for (int j = 0; j <= lon; ++j) {
            float ph = 2 * kPi * float(j) / float(lon);
            glm::vec3 s(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
            m.vertices.push_back(sv(c + s * r, glm::normalize(s / r), color, bone));
        }
    }
    for (int i = 0; i < lat; ++i)
        for (int j = 0; j < lon; ++j) {
            uint32_t a = base + uint32_t(i * (lon + 1) + j), b = a + 1, d = a + uint32_t(lon + 1), e = d + 1;
            m.indices.insert(m.indices.end(), {a, b, d, b, e, d});
        }
}

void addBox(SkinMeshData& m, int bone, glm::vec3 c, glm::vec3 h, glm::vec3 color) {
    const glm::vec3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const auto& f : n) {
        glm::vec3 u = std::fabs(f.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::vec3 v = glm::cross(f, u);
        uint32_t base = uint32_t(m.vertices.size());
        for (int k = 0; k < 4; ++k) {
            float su = (k == 1 || k == 2) ? 1.0f : -1.0f, svv = (k >= 2) ? 1.0f : -1.0f;
            glm::vec3 p = c + (f + u * su + v * svv) * h;
            m.vertices.push_back(sv(p, f, color, bone));
        }
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

} // namespace

void Character::init() {
    buildSkeleton();
    buildClips();
    buildMesh();
}

void Character::buildSkeleton() {
    // Y up, character faces +Z, its left is +X. Units: metres.
    Skeleton& s = skeleton_;
    int root = s.add("root", -1, {0, 0, 0});
    int hips = s.add("hips", root, {0, 0.95f, 0});
    int spine = s.add("spine", hips, {0, 0.12f, 0});
    int chest = s.add("chest", spine, {0, 0.22f, 0});
    int neck = s.add("neck", chest, {0, 0.22f, 0});
    s.add("head", neck, {0, 0.10f, 0});
    for (int side = 0; side < 2; ++side) {
        float x = side == 0 ? 1.0f : -1.0f;
        const char* sfx = side == 0 ? ".L" : ".R";
        int ua = s.add(std::string("upperArm") + sfx, chest, {0.20f * x, 0.16f, 0});
        int la = s.add(std::string("lowerArm") + sfx, ua, {0, -0.28f, 0});
        s.add(std::string("hand") + sfx, la, {0, -0.26f, 0});
    }
    for (int side = 0; side < 2; ++side) {
        float x = side == 0 ? 1.0f : -1.0f;
        const char* sfx = side == 0 ? ".L" : ".R";
        int ul = s.add(std::string("upperLeg") + sfx, hips, {0.10f * x, -0.05f, 0});
        int ll = s.add(std::string("lowerLeg") + sfx, ul, {0, -0.42f, 0});
        int ft = s.add(std::string("foot") + sfx, ll, {0, -0.42f, 0});
        leg_[side][0] = ul;
        leg_[side][1] = ll;
        leg_[side][2] = ft;
    }
    s.finalize();
    palette_.assign(size_t(s.size()), glm::mat4(1.0f));
}

void Character::buildClips() {
    struct Cycle {
        float thigh[4], knee[4], foot[4], arm[4], elbow[4], bob[4], hipYaw[4];
        float lean, armOut;
    };
    // Four keys per cycle (left-leg phase): contact, mid-stance, push-off, passing.
    // The right side uses the same curves shifted half a cycle.
    auto makeCycle = [&](const char* name, const Cycle& c) {
        ClipBuilder b(skeleton_, name);
        for (int k = 0; k < 4; ++k) {
            int o = (k + 2) % 4;
            b.key(float(k) * 0.25f, {0, c.bob[k], 0})
                .rot("hips", {0, c.hipYaw[k], 0})
                .rot("spine", {c.lean * 0.5f, -c.hipYaw[k] * 0.5f, 0})
                .rot("chest", {c.lean * 0.5f, -c.hipYaw[k] * 0.6f, 0})
                .rot("head", {-c.lean * 0.7f, c.hipYaw[k] * 0.3f, 0})
                .rot("upperLeg.L", {c.thigh[k], 0, 2})
                .rot("upperLeg.R", {c.thigh[o], 0, -2})
                .rot("lowerLeg.L", {c.knee[k], 0, 0})
                .rot("lowerLeg.R", {c.knee[o], 0, 0})
                .rot("foot.L", {c.foot[k], 0, 0})
                .rot("foot.R", {c.foot[o], 0, 0})
                .rot("upperArm.L", {c.arm[k], 0, c.armOut})
                .rot("upperArm.R", {c.arm[o], 0, -c.armOut})
                .rot("lowerArm.L", {c.elbow[k], 0, 0})
                .rot("lowerArm.R", {c.elbow[o], 0, 0});
        }
        return b.build();
    };

    walk_ = makeCycle("walk", Cycle{{-26, -3, 20, -14},
                                    {5, 12, 18, 58},
                                    {-12, 2, 22, -6},
                                    {18, 2, -20, -2},
                                    {-12, -15, -26, -15},
                                    {-0.035f, 0.015f, -0.035f, 0.015f},
                                    {-6, 0, 6, 0},
                                    4.0f,
                                    6.0f});
    run_ = makeCycle("run", Cycle{{-48, -8, 30, -40},
                                  {18, 35, 25, 105},
                                  {-8, 12, 30, -10},
                                  {38, 4, -42, -4},
                                  {-80, -72, -92, -72},
                                  {-0.075f, 0.03f, -0.075f, 0.03f},
                                  {-9, 0, 9, 0},
                                  13.0f,
                                  9.0f});

    ClipBuilder idle(skeleton_, "idle");
    for (int k = 0; k < 2; ++k) {
        float breathe = k == 0 ? -1.0f : 1.0f;
        idle.key(float(k) * 0.5f, {0, k == 0 ? 0.0f : -0.012f, 0})
            .rot("spine", {1.0f, 0, 0})
            .rot("chest", {1.5f * breathe, 0, 0})
            .rot("head", {-2.0f - breathe, 2.0f * breathe, 0})
            .rot("upperArm.L", {3.0f + breathe, 0, 7.0f + breathe})
            .rot("upperArm.R", {3.0f - breathe, 0, -7.0f - breathe})
            .rot("lowerArm.L", {-12, 0, 0})
            .rot("lowerArm.R", {-12, 0, 0})
            .rot("upperLeg.L", {-4, 0, 4})
            .rot("upperLeg.R", {-2, 0, -4})
            .rot("lowerLeg.L", {7, 0, 0})
            .rot("lowerLeg.R", {5, 0, 0})
            .rot("foot.L", {-3, 0, 0})
            .rot("foot.R", {-3, 0, 0});
    }
    idle_ = idle.build();

    ClipBuilder air(skeleton_, "air");
    air.key(0.0f, {0, 0.05f, 0})
        .rot("spine", {6, 0, 0})
        .rot("upperLeg.L", {-50, 0, 4})
        .rot("lowerLeg.L", {75, 0, 0})
        .rot("upperLeg.R", {-12, 0, -4})
        .rot("lowerLeg.R", {40, 0, 0})
        .rot("foot.L", {15, 0, 0})
        .rot("foot.R", {20, 0, 0})
        .rot("upperArm.L", {-30, 0, 38})
        .rot("upperArm.R", {-30, 0, -38})
        .rot("lowerArm.L", {-35, 0, 0})
        .rot("lowerArm.R", {-35, 0, 0});
    air_ = air.build();
}

void Character::buildMesh() {
    const Skeleton& s = skeleton_;
    auto J = [&](const char* n) { return s.restJoints()[size_t(s.find(n))]; };
    auto B = [&](const char* n) { return s.find(n); };

    const glm::vec3 shirt = srgb(0.86f, 0.30f, 0.14f), pants = srgb(0.20f, 0.27f, 0.42f);
    const glm::vec3 skin = srgb(0.93f, 0.74f, 0.60f), hair = srgb(0.24f, 0.14f, 0.08f);
    const glm::vec3 shoes = srgb(0.18f, 0.12f, 0.09f), pack = srgb(0.50f, 0.36f, 0.20f);
    const glm::vec3 dark = srgb(0.05f, 0.04f, 0.04f);

    SkinMeshData m;
    // torso
    addLimb(m, B("hips"), -1, J("hips") + glm::vec3(0, -0.08f, 0), J("spine"), 0.15f, 0.15f, pants, 0, {1.0f, 0.72f});
    addLimb(m, B("spine"), B("hips"), J("spine"), J("chest"), 0.145f, 0.16f, shirt, 0.35f, {1.0f, 0.72f});
    addLimb(m, B("chest"), B("spine"), J("chest"), J("neck") + glm::vec3(0, -0.01f, 0), 0.16f, 0.12f, shirt, 0.3f,
            {1.1f, 0.72f});
    addLimb(m, B("neck"), B("chest"), J("neck") - glm::vec3(0, 0.03f, 0), J("head") + glm::vec3(0, 0.02f, 0), 0.05f,
            0.045f, skin, 0.3f, {1, 1}, 8, 2);
    // head
    glm::vec3 hc = J("head") + glm::vec3(0, 0.11f, 0.01f);
    addEllipsoid(m, B("head"), hc, {0.105f, 0.125f, 0.115f}, skin);
    addEllipsoid(m, B("head"), hc + glm::vec3(0, 0.045f, -0.012f), {0.112f, 0.095f, 0.118f}, hair);
    addBox(m, B("head"), hc + glm::vec3(0.038f, 0.012f, 0.105f), {0.012f, 0.016f, 0.008f}, dark);
    addBox(m, B("head"), hc + glm::vec3(-0.038f, 0.012f, 0.105f), {0.012f, 0.016f, 0.008f}, dark);
    addBox(m, B("head"), hc + glm::vec3(0.0f, -0.02f, 0.115f), {0.012f, 0.02f, 0.014f}, skin * 0.9f);
    // backpack
    addBox(m, B("chest"), J("chest") + glm::vec3(0, 0.07f, -0.17f), {0.13f, 0.16f, 0.07f}, pack);
    addBox(m, B("chest"), J("chest") + glm::vec3(0, -0.06f, -0.245f), {0.10f, 0.05f, 0.02f}, pack * 0.7f);

    for (int side = 0; side < 2; ++side) {
        std::string sx = side == 0 ? ".L" : ".R";
        auto n = [&](const char* base) { return std::string(base) + sx; };
        int ua = B(n("upperArm").c_str()), la = B(n("lowerArm").c_str()), hd = B(n("hand").c_str());
        int ul = B(n("upperLeg").c_str()), ll = B(n("lowerLeg").c_str()), ft = B(n("foot").c_str());
        glm::vec3 jUa = J(n("upperArm").c_str()), jLa = J(n("lowerArm").c_str()), jHd = J(n("hand").c_str());
        glm::vec3 jUl = J(n("upperLeg").c_str()), jLl = J(n("lowerLeg").c_str()), jFt = J(n("foot").c_str());

        addLimb(m, ua, B("chest"), jUa + glm::vec3(0, 0.03f, 0), jLa, 0.058f, 0.046f, shirt, 0.25f);
        addLimb(m, la, ua, jLa, jHd, 0.042f, 0.034f, skin, 0.3f);
        addEllipsoid(m, hd, jHd + glm::vec3(0, -0.05f, 0.005f), {0.034f, 0.058f, 0.026f}, skin, 6, 8);
        addLimb(m, ul, B("hips"), jUl + glm::vec3(0, 0.04f, 0), jLl, 0.088f, 0.064f, pants, 0.25f);
        addLimb(m, ll, ul, jLl, jFt + glm::vec3(0, 0.02f, 0), 0.062f, 0.046f, pants, 0.3f);
        addBox(m, ft, jFt + glm::vec3(0, -0.025f, 0.055f), {0.052f, 0.035f, 0.115f}, shoes);
    }
    mesh_.upload(m);
}

void Character::spawn(const glm::vec3& p, float yaw) {
    position_ = p;
    yaw_ = yaw;
    velocity_ = glm::vec3(0.0f);
    grounded_ = true;
}

void Character::update(float dt, const CharacterInput& in, const Terrain& terrain) {
    // ---- controller ---------------------------------------------------------
    const float cy = in.cameraYaw;
    glm::vec3 fwd(-std::sin(cy), 0, -std::cos(cy)), right(std::cos(cy), 0, -std::sin(cy));
    glm::vec3 wish = fwd * in.move.y + right * in.move.x;
    float wl = glm::length(wish);
    if (wl > 1.0f) wish /= wl;

    glm::vec3 hv(velocity_.x, 0, velocity_.z);
    glm::vec3 desired = wish * (in.run ? tuning.runSpeed : tuning.walkSpeed);
    glm::vec3 diff = desired - hv;
    float maxStep = tuning.acceleration * dt * (grounded_ ? 1.0f : 0.3f);
    if (glm::length(diff) > maxStep) diff = glm::normalize(diff) * maxStep;
    hv += diff;

    glm::vec3 next = position_ + hv * dt;
    if (terrain.heightAt(next.x, next.z) < -0.9f) {
        hv = glm::vec3(0.0f);  // don't wade into deep water
    } else {
        position_.x = next.x;
        position_.z = next.z;
    }
    if (glm::length(hv) > 0.15f) {
        float target = std::atan2(hv.x, hv.z);
        yaw_ = wrapAngle(yaw_ + wrapAngle(target - yaw_) * (1.0f - std::exp(-tuning.turnRate * dt)));
    }

    float ground = terrain.heightAt(position_.x, position_.z);
    if (grounded_ && in.jump) {
        velocity_.y = tuning.jumpVelocity;
        grounded_ = false;
    }
    if (!grounded_) {
        velocity_.y -= tuning.gravity * dt;
        position_.y += velocity_.y * dt;
        if (position_.y <= ground && velocity_.y <= 0.0f) {
            position_.y = ground;
            velocity_.y = 0.0f;
            grounded_ = true;
        }
    } else {
        position_.y = ground;
        velocity_.y = 0.0f;
    }
    velocity_.x = hv.x;
    velocity_.z = hv.z;

    // ---- locomotion blend ---------------------------------------------------
    float s = speed();
    float runBlend = std::clamp((s - tuning.walkSpeed) / (tuning.runSpeed - tuning.walkSpeed), 0.0f, 1.0f);
    float moving = std::clamp(s / 1.0f, 0.0f, 1.0f);
    moving = moving * moving * (3 - 2 * moving);
    weightAir = approach(weightAir, grounded_ ? 0.0f : 1.0f, 12.0f, dt);

    // Phase advances with distance travelled, so feet don't slide at any speed.
    float stride = glm::mix(tuning.walkStride, tuning.runStride, runBlend);
    if (s > 0.02f) cyclePhase_ = std::fmod(cyclePhase_ + s * dt / stride, 1.0f);
    idlePhase_ = std::fmod(idlePhase_ + dt / 3.2f, 1.0f);

    Pose loco = blendPoses(walk_.sample(cyclePhase_), run_.sample(cyclePhase_), runBlend);
    Pose pose = blendPoses(idle_.sample(idlePhase_), loco, moving);
    pose = blendPoses(pose, air_.sample(0.0f), weightAir);
    weightIdle = (1 - moving) * (1 - weightAir);
    weightWalk = moving * (1 - runBlend) * (1 - weightAir);
    weightRun = moving * runBlend * (1 - weightAir);

    glm::mat4 root = glm::rotate(glm::translate(glm::mat4(1.0f), position_), yaw_, glm::vec3(0, 1, 0));
    ikWeight_ = approach(ikWeight_, (tuning.footIK && grounded_) ? 1.0f : 0.0f, 15.0f, dt);
    if (ikWeight_ > 0.001f) {
        applyFootIK(pose, root, terrain, dt);
    } else {
        pelvisOffset = 0.0f;
        skeleton_.computeGlobals(pose, root, globals_);
    }

    const auto& inv = skeleton_.inverseBind();
    for (size_t i = 0; i < globals_.size(); ++i) palette_[i] = globals_[i] * inv[i];
}

void Character::applyFootIK(Pose& pose, const glm::mat4& root, const Terrain& terrain, float dt) {
    skeleton_.computeGlobals(pose, root, globals_);
    const float rootY = position_.y;

    glm::vec3 targets[2];
    float lift[2];
    float drop = 0.0f;
    for (int s = 0; s < 2; ++s) {
        glm::vec3 ankle(globals_[size_t(leg_[s][2])][3]);
        lift[s] = ankle.y - rootY - kAnkleHeight;  // height the clip lifts this foot above flat ground
        float g = terrain.heightAt(ankle.x, ankle.z);
        // small lifts are treated as contact so planted feet really touch the ground
        float ty = g + kAnkleHeight + std::max(lift[s] - 0.05f, 0.0f);
        targets[s] = {ankle.x, ty, ankle.z};
        drop = std::min(drop, ty - ankle.y);
    }
    // Lower the pelvis so the foot on the lower ground can reach it.
    drop = std::max(drop, -0.4f);
    pelvisOffset = approach(pelvisOffset, drop, 14.0f, dt);
    pose.hipsOffset.y += pelvisOffset * ikWeight_;
    skeleton_.computeGlobals(pose, root, globals_);

    for (int s = 0; s < 2; ++s) {
        size_t a = size_t(leg_[s][0]), b = size_t(leg_[s][1]), c = size_t(leg_[s][2]);
        glm::vec3 pa(globals_[a][3]), pb(globals_[b][3]), pc(globals_[c][3]);
        glm::vec3 target = glm::mix(pc, targets[s], ikWeight_);
        glm::quat ga = glm::quat_cast(glm::mat3(globals_[a])), gb = glm::quat_cast(glm::mat3(globals_[b]));
        glm::vec3 hinge = glm::mat3(globals_[a]) * glm::vec3(1, 0, 0);
        solveTwoBoneIK(pa, pb, pc, target, ga, gb, pose.local[a], pose.local[b], hinge);
        ikTargets_[s] = target;
    }
    skeleton_.computeGlobals(pose, root, globals_);

    // Tilt planted feet to the slope.
    for (int s = 0; s < 2; ++s) {
        size_t c = size_t(leg_[s][2]), parent = size_t(leg_[s][1]);
        glm::vec3 ankle(globals_[c][3]);
        glm::vec3 n = terrain.normalAt(ankle.x, ankle.z);
        float plant = std::clamp(1.0f - lift[s] / 0.1f, 0.0f, 1.0f) * ikWeight_;
        glm::quat delta = glm::slerp(glm::quat(1, 0, 0, 0), rotationBetween({0, 1, 0}, n), plant);
        glm::quat parentQ = glm::quat_cast(glm::mat3(globals_[parent]));
        pose.local[c] = glm::inverse(parentQ) * delta * parentQ * pose.local[c];
    }
    skeleton_.computeGlobals(pose, root, globals_);
}

void Character::appendDebugLines(DebugLineList& out) const {
    for (int i = 0; i < skeleton_.size(); ++i) {
        int p = skeleton_.bone(i).parent;
        if (p <= 0) continue;  // skip root -> hips
        out.push_back({glm::vec3(globals_[size_t(p)][3]), glm::vec3(globals_[size_t(i)][3]), {1.0f, 0.85f, 0.1f}});
    }
    for (const auto& g : globals_) addCross(out, glm::vec3(g[3]), 0.02f, {1.0f, 0.3f, 0.1f});
    if (ikWeight_ > 0.01f)
        for (const auto& t : ikTargets_) addCross(out, t, 0.07f, {0.1f, 0.9f, 1.0f});
    glm::vec3 p = position_ + glm::vec3(0, 0.02f, 0);
    out.push_back({p, p + glm::vec3(velocity_.x, 0, velocity_.z) * 0.5f, {0.3f, 1.0f, 0.3f}});
}

} // namespace isle
