#pragma once
#include "anim/Character.h"
#include "gfx/Mesh.h"
#include "scene/Terrain.h"
#include "scene/TerrainLod.h"

#include <memory>
#include <vector>

namespace isle {

struct PointLight {
    glm::vec3 position;
    float radius;
    glm::vec3 color;
    float intensity;
};

// Everything the renderer needs to know about the sky for the current time of day.
struct SkyState {
    glm::vec3 lightDir;     // direction TO the main light (sun by day, moon by night)
    glm::vec3 lightColor;   // radiance of the main light
    glm::vec3 zenith;
    glm::vec3 horizon;
    glm::vec3 ground;
    float night;            // 0 = full day, 1 = full night
};

struct PropBatch {
    MeshData data;                   // local-space mesh
    std::vector<glm::mat4> instances;  // world transforms
    float roughness = 0.8f;
    float metallic = 0.0f;
    float emissive = 0.0f;
    bool castShadow = true;
};

class World {
public:
    void init(uint32_t seed);
    void update(float dt);
    SkyState sky() const;

    Terrain terrain;
    TerrainLod terrainLod;
    Mesh waterMesh;
    float waterCellSize = 0.0f;
    std::vector<PropBatch> props;
    std::vector<PointLight> lights;
    Character player;
    glm::vec3 spawnPoint{0.0f};
    float spawnYaw = 0.0f;

    float timeOfDay = 16.8f;  // hours [0,24)
    bool autoTime = false;
    float hoursPerSecond = 0.25f;
    float elapsed = 0.0f;
};

} // namespace isle
