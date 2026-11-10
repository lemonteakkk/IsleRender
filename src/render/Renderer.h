#pragma once
#include "gfx/GpuProfiler.h"
#include "gfx/Shader.h"
#include "render/DebugDraw.h"
#include "render/PropRenderer.h"
#include "scene/Camera.h"
#include "scene/Frustum.h"
#include "scene/World.h"

#include <array>

namespace isle {

enum class DebugView : int {
    Final = 0, Albedo, Normal, Roughness, Metallic, SSAO, ShadowCascades, Depth, Count
};
const char* debugViewName(DebugView v);

struct RenderSettings {
    DebugView debugView = DebugView::Final;
    bool wireframe = false;

    bool shadows = true;
    float shadowDistance = 260.0f;
    float cascadeLambda = 0.75f;   // practical split scheme: 0 = uniform, 1 = logarithmic
    float shadowBias = 0.0008f;
    float normalOffset = 0.06f;

    bool ssao = true;
    float ssaoRadius = 1.4f;
    float ssaoPower = 1.6f;

    bool bloom = true;
    float bloomStrength = 0.05f;
    float exposure = 1.0f;
    bool fxaa = true;

    float fogDensity = 0.0011f;
    bool pointLights = true;
    bool showSkeleton = false;
    bool proceduralWater = false;  // old per-pixel noise water detail (A/B testing)
};

// Deferred renderer:
//   CSM shadow -> G-buffer -> SSAO (half res) -> PBR lighting + sky -> forward water
//   -> bloom (dual filter) -> ACES tonemap -> FXAA
class Renderer {
public:
    static constexpr int kCascades = 3;
    static constexpr int kShadowRes = 2048;
    static constexpr int kMaxLights = 64;
    static constexpr int kBloomMips = 6;

    bool init(int width, int height);
    void resize(int width, int height);
    void render(const World& world, const Camera& camera);
    void hotReload();
    bool reloadAll();

    GLuint finalFramebuffer() const { return ldrFbo_[finalIndex_]; }
    int width() const { return width_; }
    int height() const { return height_; }

    RenderSettings settings;
    GpuProfiler profiler;
    int drawCalls = 0;
    long long trianglesDrawn = 0;

private:
    void createTargets();
    void destroyTargets();
    void updateCascades(const Camera& cam, const glm::vec3& lightDir, float aspect);
    void drawScene(const World& world, Shader& staticShader, Shader& instancedShader, bool shadowPass, int pass,
                   const Frustum& frustum);
    void fullscreen() const;

    int width_ = 0, height_ = 0;

    // shaders
    Shader terrainGBuf_, objectGBuf_, shadowStatic_, shadowInstanced_;
    Shader ssao_, ssaoBlur_, lighting_, water_, bloomDown_, bloomUp_, tonemap_, fxaa_;
    Shader skinnedGBuf_, shadowSkinned_;
    DebugDraw debug_;
    std::array<Shader*, 16> allShaders_{};
    PropRenderer props_;
    bool propsReady_ = false;

    // buffers
    GLuint frameUbo_ = 0, lightUbo_ = 0, boneUbo_ = 0, emptyVao_ = 0, waterDetailTex_ = 0;

    // shadow
    GLuint shadowTex_ = 0, shadowFbo_ = 0;
    glm::mat4 lightVP_[kCascades];
    float cascadeFar_[kCascades] = {};

    // G-buffer
    GLuint gAlbedo_ = 0, gNormal_ = 0, gMaterial_ = 0, gDepth_ = 0, gFbo_ = 0;
    // SSAO
    GLuint ssaoTex_ = 0, ssaoBlurTex_ = 0, ssaoFbo_ = 0, ssaoBlurFbo_ = 0, noiseTex_ = 0;
    std::array<glm::vec3, 16> kernel_{};
    // HDR scene + copies for water refraction
    GLuint hdrTex_ = 0, hdrFbo_ = 0, hdrCopy_ = 0, depthCopy_ = 0;
    // bloom chain
    GLuint bloomTex_[kBloomMips] = {}, bloomFbo_[kBloomMips] = {};
    int bloomW_[kBloomMips] = {}, bloomH_[kBloomMips] = {};
    // LDR ping-pong (tonemap -> fxaa)
    GLuint ldrTex_[2] = {}, ldrFbo_[2] = {};
    int finalIndex_ = 0;
    glm::vec3 lodOrigin_{0.0f};
    TerrainLod::Selection terrainSel_;

public:
    int terrainTilesDrawn = 0;  // camera pass, for the UI
    PropRenderer& props() { return props_; }
};

} // namespace isle
