#include "render/Renderer.h"

#include "gfx/GLUtil.h"
#include "render/WaterTextures.h"
#include "scene/Noise.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>

namespace isle {

const char* debugViewName(DebugView v) {
    switch (v) {
        case DebugView::Final: return "Final";
        case DebugView::Albedo: return "Albedo";
        case DebugView::Normal: return "World normal";
        case DebugView::Roughness: return "Roughness";
        case DebugView::Metallic: return "Metallic / emissive";
        case DebugView::SSAO: return "SSAO";
        case DebugView::ShadowCascades: return "Shadow cascades";
        case DebugView::Depth: return "Linear depth";
        default: return "?";
    }
}

namespace {

// Must match the std140 `Frame` block in common.glsl.
struct FrameData {
    glm::mat4 view, proj, viewProj, invViewProj, invView, invProj;
    glm::mat4 lightVP[4];
    glm::vec4 camPos;      // xyz, time (s)
    glm::vec4 lightDir;    // xyz, night factor
    glm::vec4 lightColor;  // rgb
    glm::vec4 zenith, horizon, ground;
    glm::vec4 cascadeFar;  // xyz = far distance of each cascade, w = shadows enabled
    glm::vec4 fog;         // x density
    glm::vec4 screen;      // w, h, 1/w, 1/h
    glm::vec4 misc;        // debug view, shadow bias, normal offset, point light count
};

struct LightData {
    glm::vec4 posRadius[Renderer::kMaxLights];
    glm::vec4 colorIntensity[Renderer::kMaxLights];
};

} // namespace

bool Renderer::init(int width, int height) {
    allShaders_ = {&terrainGBuf_, &objectGBuf_, &shadowStatic_, &shadowInstanced_, &ssao_,
                   &ssaoBlur_,    &lighting_,   &water_,        &bloomDown_,       &bloomUp_,
                   &tonemap_,     &fxaa_,       &skinnedGBuf_,  &shadowSkinned_,   &debug_.shader(),
                   &props_.shader()};
    bool ok = true;
    ok &= skinnedGBuf_.load("gbuffer.vert", "object.frag", {"SKINNED"});
    ok &= shadowSkinned_.load("shadow.vert", "shadow.frag", {"SKINNED"});
    ok &= debug_.init();
    ok &= terrainGBuf_.load("gbuffer.vert", "terrain.frag");
    ok &= objectGBuf_.load("gbuffer.vert", "object.frag", {"INSTANCED"});
    ok &= shadowStatic_.load("shadow.vert", "shadow.frag");
    ok &= shadowInstanced_.load("shadow.vert", "shadow.frag", {"INSTANCED"});
    ok &= ssao_.load("fullscreen.vert", "ssao.frag");
    ok &= ssaoBlur_.load("fullscreen.vert", "ssao_blur.frag");
    ok &= lighting_.load("fullscreen.vert", "lighting.frag");
    ok &= water_.load("water.vert", "water.frag");
    ok &= bloomDown_.load("fullscreen.vert", "bloom_down.frag");
    ok &= bloomUp_.load("fullscreen.vert", "bloom_up.frag");
    ok &= tonemap_.load("fullscreen.vert", "tonemap.frag");
    ok &= fxaa_.load("fullscreen.vert", "fxaa.frag");
    if (!ok) return false;

    glCreateBuffers(1, &frameUbo_);
    glNamedBufferStorage(frameUbo_, sizeof(FrameData), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glCreateBuffers(1, &lightUbo_);
    glNamedBufferStorage(lightUbo_, sizeof(LightData), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glCreateBuffers(1, &boneUbo_);
    glNamedBufferStorage(boneUbo_, sizeof(glm::mat4) * Character::kMaxBones, nullptr, GL_DYNAMIC_STORAGE_BIT);
    glCreateVertexArrays(1, &emptyVao_);
    waterDetailTex_ = createWaterDetailTexture();

    // Cascaded shadow map: one depth layer per cascade, hardware PCF via compare mode.
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &shadowTex_);
    glTextureStorage3D(shadowTex_, 1, GL_DEPTH_COMPONENT32F, kShadowRes, kShadowRes, kCascades);
    glTextureParameteri(shadowTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(shadowTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(shadowTex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(shadowTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1, 1, 1, 1};
    glTextureParameterfv(shadowTex_, GL_TEXTURE_BORDER_COLOR, border);
    glTextureParameteri(shadowTex_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(shadowTex_, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glCreateFramebuffers(1, &shadowFbo_);
    glNamedFramebufferTextureLayer(shadowFbo_, GL_DEPTH_ATTACHMENT, shadowTex_, 0, 0);
    glNamedFramebufferDrawBuffer(shadowFbo_, GL_NONE);
    glNamedFramebufferReadBuffer(shadowFbo_, GL_NONE);
    checkFramebuffer(shadowFbo_, "shadow");

    // SSAO kernel: hemisphere samples biased toward the origin; 4x4 rotation noise.
    for (int i = 0; i < 16; ++i) {
        glm::vec3 s(hash01(i, 0, 99) * 2 - 1, hash01(i, 1, 99) * 2 - 1, hash01(i, 2, 99));
        s = glm::normalize(s) * hash01(i, 3, 99);
        float t = float(i) / 16.0f;
        kernel_[i] = s * (0.1f + 0.9f * t * t);
    }
    glm::vec4 noise[16];
    for (int i = 0; i < 16; ++i) noise[i] = {hash01(i, 0, 7) * 2 - 1, hash01(i, 1, 7) * 2 - 1, 0, 0};
    noiseTex_ = createTexture2D(GL_RGBA16F, 4, 4, 1, GL_NEAREST, GL_NEAREST, GL_REPEAT);
    glTextureSubImage2D(noiseTex_, 0, 0, 0, 4, 4, GL_RGBA, GL_FLOAT, noise);

    resize(width, height);
    return true;
}

void Renderer::resize(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    destroyTargets();
    createTargets();
}

void Renderer::createTargets() {
    const int w = width_, h = height_;
    gAlbedo_ = createTexture2D(GL_RGBA8, w, h, 1, GL_NEAREST, GL_NEAREST);
    gNormal_ = createTexture2D(GL_RGBA16F, w, h, 1, GL_NEAREST, GL_NEAREST);
    gMaterial_ = createTexture2D(GL_RGBA8, w, h, 1, GL_NEAREST, GL_NEAREST);
    gDepth_ = createTexture2D(GL_DEPTH_COMPONENT32F, w, h, 1, GL_NEAREST, GL_NEAREST);
    GLuint g[] = {gAlbedo_, gNormal_, gMaterial_};
    gFbo_ = createFramebuffer(g, 3, gDepth_);
    checkFramebuffer(gFbo_, "gbuffer");

    const int hw = std::max(w / 2, 1), hh = std::max(h / 2, 1);
    ssaoTex_ = createTexture2D(GL_R8, hw, hh);
    ssaoBlurTex_ = createTexture2D(GL_R8, hw, hh);
    ssaoFbo_ = createFramebuffer(&ssaoTex_, 1, 0);
    ssaoBlurFbo_ = createFramebuffer(&ssaoBlurTex_, 1, 0);

    hdrTex_ = createTexture2D(GL_RGBA16F, w, h);
    hdrFbo_ = createFramebuffer(&hdrTex_, 1, gDepth_);  // shares depth so water can depth-test
    checkFramebuffer(hdrFbo_, "hdr");
    hdrCopy_ = createTexture2D(GL_RGBA16F, w, h);
    depthCopy_ = createTexture2D(GL_DEPTH_COMPONENT32F, w, h, 1, GL_NEAREST, GL_NEAREST);

    int bw = w, bh = h;
    for (int i = 0; i < kBloomMips; ++i) {
        bw = std::max(bw / 2, 1);
        bh = std::max(bh / 2, 1);
        bloomW_[i] = bw;
        bloomH_[i] = bh;
        bloomTex_[i] = createTexture2D(GL_R11F_G11F_B10F, bw, bh);
        bloomFbo_[i] = createFramebuffer(&bloomTex_[i], 1, 0);
    }
    for (int i = 0; i < 2; ++i) {
        ldrTex_[i] = createTexture2D(GL_RGBA8, w, h);
        ldrFbo_[i] = createFramebuffer(&ldrTex_[i], 1, 0);
    }
}

void Renderer::destroyTargets() {
    for (GLuint* t : {&gAlbedo_, &gNormal_, &gMaterial_, &gDepth_, &ssaoTex_, &ssaoBlurTex_, &hdrTex_, &hdrCopy_,
                      &depthCopy_, &ldrTex_[0], &ldrTex_[1]})
        deleteTexture(*t);
    for (GLuint* f : {&gFbo_, &ssaoFbo_, &ssaoBlurFbo_, &hdrFbo_, &ldrFbo_[0], &ldrFbo_[1]}) deleteFramebuffer(*f);
    for (int i = 0; i < kBloomMips; ++i) {
        deleteTexture(bloomTex_[i]);
        deleteFramebuffer(bloomFbo_[i]);
    }
}

void Renderer::hotReload() {
    for (Shader* s : allShaders_)
        if (s->reloadIfChanged()) std::printf("[shader] hot-reloaded program %u\n", s->id());
}

bool Renderer::reloadAll() {
    bool ok = true;
    for (Shader* s : allShaders_) ok &= s->reload();
    return ok;
}

void Renderer::updateCascades(const Camera& cam, const glm::vec3& lightDir, float aspect) {
    const float n = std::max(cam.nearPlane, 1.0f);  // splits from 1 m, not the near plane
    const float f = std::min(settings.shadowDistance, cam.farPlane);
    const glm::mat4 view = cam.view();
    const glm::vec3 up = std::fabs(lightDir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    float prev = n;
    for (int c = 0; c < kCascades; ++c) {
        float p = float(c + 1) / float(kCascades);
        float logSplit = n * std::pow(f / n, p);
        float uniSplit = n + (f - n) * p;
        float split = settings.cascadeLambda * logSplit + (1.0f - settings.cascadeLambda) * uniSplit;

        glm::mat4 inv = glm::inverse(glm::perspective(cam.fovY, aspect, prev, split) * view);
        glm::vec3 corners[8];
        glm::vec3 center(0.0f);
        for (int i = 0; i < 8; ++i) {
            glm::vec4 p4 = inv * glm::vec4((i & 1) ? 1 : -1, (i & 2) ? 1 : -1, (i & 4) ? 1 : -1, 1);
            corners[i] = glm::vec3(p4) / p4.w;
            center += corners[i];
        }
        center /= 8.0f;
        // Bounding sphere keeps the projection size constant under rotation -> no shimmering.
        float radius = 0.0f;
        for (auto& v : corners) radius = std::max(radius, glm::length(v - center));
        radius = std::ceil(radius * 16.0f) / 16.0f;

        const float backoff = radius + 200.0f;  // catch casters (mountains) between light and slice
        glm::mat4 lightView = glm::lookAt(center + lightDir * backoff, center, up);
        glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, backoff + radius);

        // Snap to whole shadow-map texels so the cascade does not crawl when the camera moves.
        glm::vec4 origin = lightProj * lightView * glm::vec4(0, 0, 0, 1);
        origin *= float(kShadowRes) * 0.5f;
        glm::vec4 offset = (glm::round(origin) - origin) * (2.0f / float(kShadowRes));
        lightProj[3][0] += offset.x;
        lightProj[3][1] += offset.y;

        lightVP_[c] = lightProj * lightView;
        cascadeFar_[c] = split;
        prev = split;
    }
}

void Renderer::drawScene(const World& world, Shader& staticShader, Shader& instancedShader, bool shadowPass, int pass,
                         const Frustum& frustum) {
    staticShader.use();
    staticShader.set("uModel", glm::mat4(1.0f));
    world.terrainLod.select(frustum, lodOrigin_, terrainSel_);
    world.terrainLod.draw(terrainSel_);
    drawCalls += world.terrainLod.useMultiDraw ? 1 : terrainSel_.tiles;
    trianglesDrawn += terrainSel_.triangles;

    // GPU-culled props: one indirect multi-draw for every batch
    instancedShader.use();
    instancedShader.set("uModel", glm::mat4(1.0f));
    if (!shadowPass) {
        instancedShader.set("uRoughness", -1.0f);  // use the per-vertex roughness baked into uv.y
        instancedShader.set("uMetallic", 0.0f);
        instancedShader.set("uEmissive", 6.0f);    // only lantern lamps carry an emissive mask
    }
    props_.draw(pass);
    drawCalls += props_.drawCallsPerPass();
    trianglesDrawn += props_.visibleTriangles[pass];

    // GPU-skinned player
    Shader& sk = shadowPass ? shadowSkinned_ : skinnedGBuf_;
    sk.use();
    if (!shadowPass) {
        sk.set("uRoughness", 0.7f);
        sk.set("uMetallic", 0.0f);
        sk.set("uEmissive", 0.0f);
    }
    world.player.mesh().draw();
    ++drawCalls;
    trianglesDrawn += world.player.mesh().indexCount() / 3;
}

void Renderer::fullscreen() const {
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Renderer::render(const World& world, const Camera& cam) {
    profiler.beginFrame();
    drawCalls = 0;
    trianglesDrawn = 0;

    const float aspect = float(width_) / float(height_);
    const SkyState sky = world.sky();
    const glm::mat4 view = cam.view();
    const glm::mat4 proj = cam.projection(aspect);
    const bool shadowsOn = settings.shadows && glm::length(sky.lightColor) > 1e-4f;
    lodOrigin_ = cam.position();
    if (shadowsOn) updateCascades(cam, sky.lightDir, aspect);

    // GPU culling of all prop instances for the 3 cascades + camera, recorded up front.
    if (!propsReady_) {
        propsReady_ = props_.init(world);
        if (!propsReady_) std::fprintf(stderr, "[props] GPU culling init failed\n");
    }
    {
        Frustum frustums[PropRenderer::kPasses];
        for (int c = 0; c < kCascades; ++c) frustums[c] = Frustum::fromMatrix(lightVP_[c]);
        frustums[PropRenderer::kCameraPass] = Frustum::fromMatrix(proj * view);
        const float projScale = float(height_) / (2.0f * std::tan(cam.fovY * 0.5f));
        if (propsReady_) {
            props_.beginFrame();  // fence wait happens outside any GPU timer
            const bool gpu = props_.mode == PropRenderer::Mode::Gpu;
            if (gpu) profiler.begin("Cull (compute)");
            props_.cull(frustums, cam.position(), 1.5f, projScale);
            if (gpu) profiler.end();
        }
    }

    // ---- per-frame uniforms --------------------------------------------------
    LightData lights{};
    int lightCount = 0;
    float lanternFade = std::clamp((sky.night - 0.25f) / 0.5f, 0.0f, 1.0f);
    if (settings.pointLights && lanternFade > 0.0f) {
        for (const auto& l : world.lights) {
            if (lightCount >= kMaxLights) break;
            float flicker = 0.92f + 0.08f * std::sin(world.elapsed * 9.0f + l.position.x * 3.1f);
            lights.posRadius[lightCount] = glm::vec4(l.position, l.radius);
            lights.colorIntensity[lightCount] = glm::vec4(l.color, l.intensity * lanternFade * flicker);
            ++lightCount;
        }
    }
    glNamedBufferSubData(lightUbo_, 0, sizeof(LightData), &lights);

    FrameData fd{};
    fd.view = view;
    fd.proj = proj;
    fd.viewProj = proj * view;
    fd.invViewProj = glm::inverse(fd.viewProj);
    fd.invView = glm::inverse(view);
    fd.invProj = glm::inverse(proj);
    for (int c = 0; c < kCascades; ++c) fd.lightVP[c] = lightVP_[c];
    fd.camPos = glm::vec4(cam.position(), world.elapsed);
    fd.lightDir = glm::vec4(sky.lightDir, sky.night);
    fd.lightColor = glm::vec4(sky.lightColor, lanternFade);
    fd.zenith = glm::vec4(sky.zenith, 0);
    fd.horizon = glm::vec4(sky.horizon, 0);
    fd.ground = glm::vec4(sky.ground, 0);
    fd.cascadeFar = glm::vec4(cascadeFar_[0], cascadeFar_[1], cascadeFar_[2], shadowsOn ? 1.0f : 0.0f);
    fd.fog = glm::vec4(settings.fogDensity, 0, 0, 0);
    fd.screen = glm::vec4(float(width_), float(height_), 1.0f / float(width_), 1.0f / float(height_));
    fd.misc = glm::vec4(float(int(settings.debugView)), settings.shadowBias, settings.normalOffset, float(lightCount));
    glNamedBufferSubData(frameUbo_, 0, sizeof(FrameData), &fd);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameUbo_);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, lightUbo_);
    const auto& palette = world.player.skinMatrices();
    glNamedBufferSubData(boneUbo_, 0, GLsizeiptr(std::min<size_t>(palette.size(), Character::kMaxBones) * sizeof(glm::mat4)),
                         palette.data());
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, boneUbo_);

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // ---- 1. cascaded shadow maps ---------------------------------------------
    profiler.begin("Shadow maps");
    if (shadowsOn) {
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
        glViewport(0, 0, kShadowRes, kShadowRes);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 3.0f);
        for (int c = 0; c < kCascades; ++c) {
            glNamedFramebufferTextureLayer(shadowFbo_, GL_DEPTH_ATTACHMENT, shadowTex_, 0, c);
            glClear(GL_DEPTH_BUFFER_BIT);
            shadowStatic_.set("uLightMatrix", lightVP_[c]);
            shadowInstanced_.set("uLightMatrix", lightVP_[c]);
            shadowSkinned_.set("uLightMatrix", lightVP_[c]);
            drawScene(world, shadowStatic_, shadowInstanced_, true, c, Frustum::fromMatrix(lightVP_[c]));
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    profiler.end();

    // ---- 2. G-buffer -----------------------------------------------------------
    profiler.begin("G-buffer");
    glBindFramebuffer(GL_FRAMEBUFFER, gFbo_);
    glViewport(0, 0, width_, height_);
    glClearColor(0, 0, 0, 0);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    if (settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    drawScene(world, terrainGBuf_, objectGBuf_, false, PropRenderer::kCameraPass, Frustum::fromMatrix(proj * view));
    terrainTilesDrawn = terrainSel_.tiles;
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    profiler.end();

    // ---- 3. SSAO (half resolution) -------------------------------------------
    profiler.begin("SSAO");
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    if (settings.ssao) {
        glBindFramebuffer(GL_FRAMEBUFFER, ssaoFbo_);
        glViewport(0, 0, std::max(width_ / 2, 1), std::max(height_ / 2, 1));
        glBindTextureUnit(0, gDepth_);
        glBindTextureUnit(1, gNormal_);
        glBindTextureUnit(2, noiseTex_);
        ssao_.setArray("uKernel", kernel_.data(), 16);
        ssao_.set("uRadius", settings.ssaoRadius);
        ssao_.set("uPower", settings.ssaoPower);
        ssao_.use();
        fullscreen();
        glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFbo_);
        glBindTextureUnit(0, ssaoTex_);
        glBindTextureUnit(1, gDepth_);
        ssaoBlur_.use();
        fullscreen();
    } else {
        const float one = 1.0f;
        glClearTexImage(ssaoBlurTex_, 0, GL_RED, GL_FLOAT, &one);
    }
    profiler.end();

    // ---- 4. deferred lighting + sky -------------------------------------------
    profiler.begin("Lighting");
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo_);
    glViewport(0, 0, width_, height_);
    glBindTextureUnit(0, gAlbedo_);
    glBindTextureUnit(1, gNormal_);
    glBindTextureUnit(2, gMaterial_);
    glBindTextureUnit(3, gDepth_);
    glBindTextureUnit(4, ssaoBlurTex_);
    glBindTextureUnit(5, shadowTex_);
    lighting_.use();
    fullscreen();
    profiler.end();

    // ---- 5. forward water ------------------------------------------------------
    profiler.begin("Water");
    if (settings.debugView == DebugView::Final || settings.wireframe) {
        glCopyImageSubData(hdrTex_, GL_TEXTURE_2D, 0, 0, 0, 0, hdrCopy_, GL_TEXTURE_2D, 0, 0, 0, 0, width_, height_, 1);
        glCopyImageSubData(gDepth_, GL_TEXTURE_2D, 0, 0, 0, 0, depthCopy_, GL_TEXTURE_2D, 0, 0, 0, 0, width_, height_,
                           1);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glBindTextureUnit(0, hdrCopy_);
        glBindTextureUnit(1, depthCopy_);
        glBindTextureUnit(2, waterDetailTex_);
        water_.set("uProceduralDetail", settings.proceduralWater ? 1 : 0);
        glBindTextureUnit(5, shadowTex_);
        glm::vec3 camPos = cam.position();
        float cell = world.waterCellSize * 4.0f;  // snap to 4 cells so wave phase stays stable
        water_.set("uWaterOffset", glm::vec2(std::floor(camPos.x / cell) * cell, std::floor(camPos.z / cell) * cell));
        water_.use();
        if (settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        world.waterMesh.draw();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        ++drawCalls;
        trianglesDrawn += world.waterMesh.indexCount() / 3;
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }
    profiler.end();

    // ---- 6. bloom: 13-tap downsample chain + tent upsample ---------------------
    profiler.begin("Bloom");
    const bool bloomOn = settings.bloom && settings.debugView == DebugView::Final;
    if (bloomOn) {
        GLuint src = hdrTex_;
        int sw = width_, sh = height_;
        bloomDown_.use();
        for (int i = 0; i < kBloomMips; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
            glViewport(0, 0, bloomW_[i], bloomH_[i]);
            glBindTextureUnit(0, src);
            bloomDown_.set("uSrcTexel", glm::vec2(1.0f / float(sw), 1.0f / float(sh)));
            bloomDown_.set("uFirstPass", i == 0 ? 1 : 0);
            fullscreen();
            src = bloomTex_[i];
            sw = bloomW_[i];
            sh = bloomH_[i];
        }
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        bloomUp_.use();
        for (int i = kBloomMips - 1; i > 0; --i) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i - 1]);
            glViewport(0, 0, bloomW_[i - 1], bloomH_[i - 1]);
            glBindTextureUnit(0, bloomTex_[i]);
            bloomUp_.set("uSrcTexel", glm::vec2(1.0f / float(bloomW_[i]), 1.0f / float(bloomH_[i])));
            fullscreen();
        }
        glDisable(GL_BLEND);
    }
    profiler.end();

    // ---- 7. tonemap + FXAA -----------------------------------------------------
    profiler.begin("Tonemap + FXAA");
    glBindFramebuffer(GL_FRAMEBUFFER, ldrFbo_[0]);
    glViewport(0, 0, width_, height_);
    glBindTextureUnit(0, hdrTex_);
    glBindTextureUnit(1, bloomTex_[0]);
    tonemap_.set("uExposure", settings.exposure);
    tonemap_.set("uBloomStrength", bloomOn ? settings.bloomStrength : 0.0f);
    tonemap_.use();
    fullscreen();
    finalIndex_ = 0;
    if (settings.fxaa && settings.debugView == DebugView::Final) {
        glBindFramebuffer(GL_FRAMEBUFFER, ldrFbo_[1]);
        glBindTextureUnit(0, ldrTex_[0]);
        fxaa_.use();
        fullscreen();
        finalIndex_ = 1;
    }
    profiler.end();

    // ---- 8. debug overlay (skeleton, IK targets) -------------------------------
    if (settings.showSkeleton) {
        DebugLineList lines;
        world.player.appendDebugLines(lines);
        glBindFramebuffer(GL_FRAMEBUFFER, ldrFbo_[finalIndex_]);
        glViewport(0, 0, width_, height_);
        debug_.draw(lines);
    }
    profiler.endFrame();
    props_.endFrame();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace isle
