#include "scene/World.h"

#include "scene/Noise.h"
#include "scene/Procedural.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>

namespace isle {

namespace {

constexpr float kPi = 3.14159265358979f;

float smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

glm::mat4 placement(glm::vec3 pos, float yaw, float scale, glm::vec3 tiltNormal, float tiltAmount) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
    glm::vec3 up(0, 1, 0);
    glm::vec3 n = glm::normalize(glm::mix(up, tiltNormal, tiltAmount));
    glm::vec3 axis = glm::cross(up, n);
    float s = glm::length(axis);
    if (s > 1e-4f) m = glm::rotate(m, std::asin(std::min(s, 1.0f)), axis / s);
    m = glm::rotate(m, yaw, up);
    return glm::scale(m, glm::vec3(scale));
}

} // namespace

void World::init(uint32_t seed) {
    terrain.generate(385, 640.0f, seed);
    terrainLod.build(terrain);

    // Water grid follows the camera; the renderer snaps it to whole cells.
    const int waterCells = 320;
    const float waterSize = 1600.0f;
    waterCellSize = waterSize / float(waterCells);
    waterMesh.upload(makeGrid(waterCells, waterSize));

    std::vector<glm::mat4> pines, rounds, rocks, lanterns;
    const float half = terrain.halfSize();
    const float cell = 5.0f;
    for (float z = -half; z < half; z += cell) {
        for (float x = -half; x < half; x += cell) {
            int ix = int(std::floor(x / cell)), iz = int(std::floor(z / cell));
            float px = x + hash01(ix, iz, seed + 1) * cell;
            float pz = z + hash01(ix, iz, seed + 2) * cell;
            float h = terrain.heightAt(px, pz);
            glm::vec3 n = terrain.normalAt(px, pz);
            float r = hash01(ix, iz, seed + 3);
            float yaw = hash01(ix, iz, seed + 4) * 2 * kPi;

            float forest = fbm(px * 0.012f, pz * 0.012f, 3, seed + 50);
            if (h > 3.0f && h < 40.0f && n.y > 0.86f && forest > 0.48f && r < 0.75f) {
                float s = 0.75f + hash01(ix, iz, seed + 5) * 0.6f;
                glm::mat4 m = placement({px, h - 0.2f, pz}, yaw, s, n, 0.0f);
                bool pine = (h > 14.0f) ? (r < 0.65f) : (r < 0.25f);
                (pine ? pines : rounds).push_back(m);
            } else if (h > 0.5f && n.y < 0.9f && r > 0.93f) {
                float s = 0.6f + hash01(ix, iz, seed + 6) * 1.6f;
                rocks.push_back(placement({px, h - 0.3f, pz}, yaw, s, n, 0.6f));
            } else if (h > -1.5f && h < 1.6f && r > 0.985f) {
                float s = 0.4f + hash01(ix, iz, seed + 7) * 0.8f;
                rocks.push_back(placement({px, h - 0.2f, pz}, yaw, s, n, 0.8f));
            }
        }
    }

    // A ring of lanterns along the coast. March outward on rays to find the shoreline.
    const int lanternCount = 18;
    for (int i = 0; i < lanternCount; ++i) {
        float a = 2 * kPi * float(i) / float(lanternCount) + 0.13f;
        glm::vec2 dir(std::cos(a), std::sin(a));
        float best = -1.0f;
        for (float d = 20.0f; d < half; d += 1.0f) {
            float h = terrain.heightAt(dir.x * d, dir.y * d);
            if (h < 3.5f) { best = d - 6.0f; break; }
        }
        if (best < 0) continue;
        glm::vec3 p(dir.x * best, 0, dir.y * best);
        p.y = terrain.heightAt(p.x, p.z) - 0.05f;
        if (p.y < 1.0f) continue;
        lanterns.push_back(placement(p, a, 1.0f, {0, 1, 0}, 0.0f));
        lights.push_back({p + glm::vec3(0, 2.3f, 0), 14.0f, glm::pow(glm::vec3(1.0f, 0.72f, 0.38f), glm::vec3(2.2f)),
                          40.0f});
    }

    auto addBatch = [&](MeshData data, const std::vector<glm::mat4>& inst, float rough, float emissive) {
        if (inst.empty()) return;
        PropBatch b;
        b.data = std::move(data);  // uploaded by the renderer's GPU-driven prop path
        b.instances = inst;
        b.roughness = rough;
        b.emissive = emissive;
        props.push_back(std::move(b));
    };
    addBatch(makePineTree(), pines, 0.85f, 0.0f);
    addBatch(makeRoundTree(), rounds, 0.8f, 0.0f);
    addBatch(makeRock(seed), rocks, 0.75f, 0.0f);
    addBatch(makeLantern(), lanterns, 0.6f, 6.0f);

    std::printf("[world] %zu pines, %zu round trees, %zu rocks, %zu lanterns\n", pines.size(), rounds.size(),
                rocks.size(), lanterns.size());

    // Player spawns on the grass just above the west beach, facing the sea.
    player.init();
    float a = std::atan2(40.0f, -110.0f);
    glm::vec2 dir(std::cos(a), std::sin(a));
    float d = 20.0f;
    while (d < half && terrain.heightAt(dir.x * d, dir.y * d) > 2.2f) d += 0.5f;  // below the tree line
    spawnPoint = glm::vec3(dir.x * d, 0.0f, dir.y * d);
    spawnPoint.y = terrain.heightAt(spawnPoint.x, spawnPoint.z);
    spawnYaw = std::atan2(dir.x, dir.y);
    player.spawn(spawnPoint, spawnYaw);
}

void World::update(float dt) {
    elapsed += dt;
    if (autoTime) timeOfDay = std::fmod(timeOfDay + dt * hoursPerSecond, 24.0f);
}

SkyState World::sky() const {
    // Sun travels an arc tilted toward -Z; it rises at 6:00 and sets at 18:00.
    float a = (timeOfDay - 6.0f) / 24.0f * 2.0f * kPi;
    glm::vec3 sun = glm::normalize(glm::vec3(std::cos(a), std::sin(a) * 0.92f, -0.38f));
    float elev = sun.y;

    float day = smooth(-0.10f, 0.12f, elev);        // 0 at night, 1 by day
    float golden = 1.0f - smooth(0.02f, 0.35f, std::fabs(elev));  // strongest near the horizon

    SkyState s;
    s.night = 1.0f - day;

    glm::vec3 dayZenith(0.09f, 0.24f, 0.68f), dayHorizon(0.42f, 0.58f, 0.84f);
    glm::vec3 duskZenith(0.16f, 0.18f, 0.42f), duskHorizon(1.05f, 0.50f, 0.26f);
    glm::vec3 nightZenith(0.006f, 0.010f, 0.030f), nightHorizon(0.025f, 0.035f, 0.070f);

    glm::vec3 zen = glm::mix(dayZenith, duskZenith, golden * 0.8f);
    glm::vec3 hor = glm::mix(dayHorizon, duskHorizon, golden);
    s.zenith = glm::mix(nightZenith, zen, day);
    s.horizon = glm::mix(nightHorizon, hor, day);
    s.ground = glm::mix(glm::vec3(0.01f, 0.012f, 0.015f), glm::vec3(0.16f, 0.15f, 0.12f), day);

    if (elev > -0.05f) {
        glm::vec3 noon(1.0f, 0.95f, 0.88f), dusk(1.0f, 0.48f, 0.22f);
        s.lightDir = sun;
        s.lightColor = glm::mix(noon, dusk, golden) * 8.0f * smooth(-0.05f, 0.08f, elev);
    } else {
        // Moon: roughly opposite the sun, cold and dim.
        glm::vec3 moon = glm::normalize(glm::vec3(-sun.x, std::max(-sun.y, 0.25f), 0.45f));
        s.lightDir = moon;
        s.lightColor = glm::vec3(0.45f, 0.55f, 0.85f) * 0.35f * smooth(-0.05f, -0.2f, elev);
    }
    return s;
}

} // namespace isle
