#include "gfx/GL.h"
#include <GLFW/glfw3.h>

#include "core/Paths.h"
#include "gfx/GLUtil.h"
#include "render/Renderer.h"
#include "scene/Camera.h"
#include "scene/World.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace isle;

namespace {

struct Options {
    std::string screenshot;   // render N frames off-screen, save PNG, exit
    int frames = 120;
    int width = 1600, height = 900;
    float timeOfDay = -1.0f;
    float yaw = NAN, pitch = NAN, distance = NAN;
    float targetX = NAN, targetZ = NAN;
    int debugView = 0;
    bool wireframe = false;
    bool freeCamera = false;     // start in free-orbit mode instead of following the player
    float moveDir = NAN;         // offscreen: simulate movement at this angle relative to the camera
    bool run = false;
    bool skeleton = false;
    bool noIK = false;
    bool bench = false;          // fixed multi-view GPU benchmark, prints a markdown table
    bool noLod = false, noCull = false, gpuCull = false, oldWater = false, multiDraw = false;  // disable terrain LOD+tile culling / GPU instance culling (A/B testing)
    float spawnX = NAN, spawnZ = NAN, spawnYaw = NAN;
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--screenshot") o.screenshot = next();
        else if (a == "--frames") o.frames = std::atoi(next());
        else if (a == "--size") std::sscanf(next(), "%dx%d", &o.width, &o.height);
        else if (a == "--time") o.timeOfDay = float(std::atof(next()));
        else if (a == "--yaw") o.yaw = float(std::atof(next()));
        else if (a == "--pitch") o.pitch = float(std::atof(next()));
        else if (a == "--dist") o.distance = float(std::atof(next()));
        else if (a == "--target") std::sscanf(next(), "%f,%f", &o.targetX, &o.targetZ);
        else if (a == "--view") o.debugView = std::atoi(next());
        else if (a == "--wireframe") o.wireframe = true;
        else if (a == "--free") o.freeCamera = true;
        else if (a == "--move") o.moveDir = float(std::atof(next()));
        else if (a == "--run") o.run = true;
        else if (a == "--skeleton") o.skeleton = true;
        else if (a == "--noik") o.noIK = true;
        else if (a == "--bench") o.bench = true;
        else if (a == "--nolod") o.noLod = true;
        else if (a == "--nocull") o.noCull = true;
        else if (a == "--gpucull") o.gpuCull = true;
        else if (a == "--oldwater") o.oldWater = true;
        else if (a == "--multidraw") o.multiDraw = true;
        else if (a == "--spawn") std::sscanf(next(), "%f,%f,%f", &o.spawnX, &o.spawnZ, &o.spawnYaw);
        else if (a == "--help") {
            std::printf(
                "IsleRender options:\n"
                "  --screenshot out.png  render off-screen, save PNG and print GPU timings\n"
                "  --frames N            frames to render before the screenshot (default 120)\n"
                "  --size WxH            framebuffer size (default 1600x900)\n"
                "  --time H              time of day in hours, e.g. 17.5\n"
                "  --yaw R --pitch R --dist M --target X,Z   camera placement\n"
                "  --view N              debug view index (0 = final)\n"
                "  --wireframe           wireframe overlay\n"
                "  --free                free camera instead of third-person follow\n"
                "  --move R [--run]      offscreen: move the player at angle R (camera relative)\n"
                "  --spawn X,Z,YAW       player spawn point\n"
                "  --skeleton            draw the skeleton / IK overlay\n"
                "  --bench               run the fixed multi-view GPU benchmark and exit\n"
                "  --nolod               disable terrain tile culling + LOD (A/B testing)\n"
                "  --nocull              disable prop instance culling\n"
                "  --gpucull             cull props with compute + indirect draws (default: CPU)\n"
                "  --oldwater            per-pixel procedural water detail (pre-optimisation)\n");
            std::exit(0);
        }
    }
    return o;
}

struct Input {
    double lastX = 0, lastY = 0;
    float scroll = 0;
    bool resized = false;
} g_input;

void scrollCallback(GLFWwindow*, double, double dy) { g_input.scroll += float(dy); }
void resizeCallback(GLFWwindow*, int, int) { g_input.resized = true; }

bool saveScreenshot(GLuint fbo, int w, int h, const std::filesystem::path& path) {
    std::vector<unsigned char> px(size_t(w) * h * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    std::vector<unsigned char> png;
    stbi_flip_vertically_on_write(1);
    stbi_write_png_to_func(
        [](void* ctx, void* data, int size) {
            auto* out = static_cast<std::vector<unsigned char>*>(ctx);
            out->insert(out->end(), static_cast<unsigned char*>(data), static_cast<unsigned char*>(data) + size);
        },
        &png, w, h, 4, px.data(), w * 4);
    return writeBinaryFile(path, png.data(), png.size());
}

void updateCamera(GLFWwindow* win, Camera& cam, const World& world, float dt, bool freeMove) {
    ImGuiIO& io = ImGui::GetIO();
    double x, y;
    glfwGetCursorPos(win, &x, &y);
    float dx = float(x - g_input.lastX), dy = float(y - g_input.lastY);
    g_input.lastX = x;
    g_input.lastY = y;

    if (!io.WantCaptureMouse) {
        bool orbit = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) || glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT);
        if (orbit) {
            cam.yaw -= dx * 0.005f;
            cam.pitch = std::clamp(cam.pitch + dy * 0.005f, -0.2f, 1.45f);
        }
        if (freeMove && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE)) {
            glm::vec3 right = glm::normalize(glm::cross(cam.forward(), glm::vec3(0, 1, 0)));
            glm::vec3 fwd = glm::normalize(glm::vec3(cam.forward().x, 0, cam.forward().z));
            cam.target += (-right * dx + fwd * dy) * cam.distance * 0.0015f;
        }
        if (g_input.scroll != 0.0f)
            cam.distance = std::clamp(cam.distance * std::pow(0.88f, g_input.scroll), freeMove ? 3.0f : 2.0f,
                                      freeMove ? 900.0f : 40.0f);
    }
    g_input.scroll = 0.0f;

    if (freeMove && !io.WantCaptureKeyboard) {
        glm::vec3 fwd = glm::normalize(glm::vec3(cam.forward().x, 0, cam.forward().z));
        glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
        glm::vec3 move(0.0f);
        if (glfwGetKey(win, GLFW_KEY_W)) move += fwd;
        if (glfwGetKey(win, GLFW_KEY_S)) move -= fwd;
        if (glfwGetKey(win, GLFW_KEY_D)) move += right;
        if (glfwGetKey(win, GLFW_KEY_A)) move -= right;
        if (glfwGetKey(win, GLFW_KEY_E)) move.y += 1.0f;
        if (glfwGetKey(win, GLFW_KEY_Q)) move.y -= 1.0f;
        float speed = std::max(cam.distance, 10.0f) * (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) ? 2.5f : 0.8f);
        cam.target += move * speed * dt;
    }
    if (freeMove) {
        float ground = std::max(world.terrain.heightAt(cam.target.x, cam.target.z), 0.0f) + 1.0f;
        cam.target.y = std::max(cam.target.y, ground);
    }
}

// Raise the orbit pitch until the eye clears the terrain (iterated because the eye's
// ground position moves as the pitch changes).
void keepCameraAboveGround(Camera& cam, const World& world, float clearance) {
    for (int i = 0; i < 4; ++i) {
        glm::vec3 eye = cam.position();
        float eyeGround = std::max(world.terrain.heightAt(eye.x, eye.z), 0.3f) + clearance;
        if (eye.y >= eyeGround) return;
        float s = std::clamp((eyeGround - cam.target.y) / cam.distance, -0.99f, 0.99f);
        cam.pitch = std::min(std::max(cam.pitch, std::asin(s)), 1.45f);
    }
}

void drawUI(Renderer& r, World& world, Camera& cam, float cpuMs, const char* gpuName, bool& showDemo, bool& follow) {
    RenderSettings& s = r.settings;
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("IsleRender");
    ImGui::Text("%s", gpuName);
    ImGui::Text("CPU %.2f ms  |  GPU %.2f ms  |  %.0f FPS", cpuMs, r.profiler.totalMs(), ImGui::GetIO().Framerate);
    ImGui::Text("%d draw calls, %.2fM triangles", r.drawCalls, double(r.trianglesDrawn) / 1e6);

    if (ImGui::CollapsingHeader("GPU passes", ImGuiTreeNodeFlags_DefaultOpen)) {
        double total = std::max(r.profiler.totalMs(), 0.001);
        for (const auto& e : r.profiler.entries()) {
            ImGui::ProgressBar(float(e.ms / total), ImVec2(150, 0), "");
            ImGui::SameLine();
            ImGui::Text("%-15s %6.2f ms", e.name.c_str(), e.ms);
        }
    }
    if (ImGui::CollapsingHeader("Time of day", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Hour", &world.timeOfDay, 0.0f, 24.0f, "%.2f h");
        ImGui::Checkbox("Animate", &world.autoTime);
        ImGui::SameLine();
        ImGui::SliderFloat("h/s", &world.hoursPerSecond, 0.02f, 2.0f);
    }
    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        int dv = int(s.debugView);
        if (ImGui::BeginCombo("View", debugViewName(s.debugView))) {
            for (int i = 0; i < int(DebugView::Count); ++i)
                if (ImGui::Selectable(debugViewName(DebugView(i)), dv == i)) s.debugView = DebugView(i);
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Wireframe", &s.wireframe);
        ImGui::SliderFloat("Exposure", &s.exposure, 0.1f, 4.0f);
        ImGui::Checkbox("Shadows", &s.shadows);
        if (s.shadows) {
            ImGui::SliderFloat("Shadow distance", &s.shadowDistance, 50.0f, 600.0f);
            ImGui::SliderFloat("Split lambda", &s.cascadeLambda, 0.0f, 1.0f);
            ImGui::SliderFloat("Depth bias", &s.shadowBias, 0.0f, 0.005f, "%.4f");
            ImGui::SliderFloat("Normal offset", &s.normalOffset, 0.0f, 0.3f);
        }
        ImGui::Checkbox("SSAO", &s.ssao);
        if (s.ssao) {
            ImGui::SliderFloat("AO radius", &s.ssaoRadius, 0.2f, 5.0f);
            ImGui::SliderFloat("AO power", &s.ssaoPower, 0.5f, 4.0f);
        }
        ImGui::Checkbox("Bloom", &s.bloom);
        if (s.bloom) ImGui::SliderFloat("Bloom strength", &s.bloomStrength, 0.0f, 0.3f);
        ImGui::Checkbox("FXAA", &s.fxaa);
        ImGui::Checkbox("Lantern lights", &s.pointLights);
        ImGui::SliderFloat("Fog density", &s.fogDensity, 0.0f, 0.01f, "%.4f");
    }
    if (ImGui::CollapsingHeader("Optimisations")) {
        TerrainLod& t = world.terrainLod;
        ImGui::Checkbox("Terrain LOD", &t.enableLod);
        ImGui::SameLine();
        ImGui::Checkbox("Tile culling", &t.enableCulling);
        ImGui::SliderFloat("LOD distance", &t.lodDistance, 20.0f, 300.0f);
        ImGui::Checkbox("Terrain glMultiDrawElements", &t.useMultiDraw);
        ImGui::Text("Terrain tiles drawn (camera): %d / %d", r.terrainTilesDrawn, t.tileCount());
        PropRenderer& p = r.props();
        ImGui::Checkbox("Prop culling", &p.cullingEnabled);
        bool gpu = p.mode == PropRenderer::Mode::Gpu;
        if (ImGui::Checkbox("GPU culling (compute + indirect)", &gpu))
            p.mode = gpu ? PropRenderer::Mode::Gpu : PropRenderer::Mode::Cpu;
        ImGui::Text("Visible props: C0 %d  C1 %d  C2 %d  cam %d  / %d", p.visibleInstances[0], p.visibleInstances[1],
                    p.visibleInstances[2], p.visibleInstances[3], p.instanceCount());
        ImGui::Checkbox("Procedural water detail (old)", &s.proceduralWater);
    }
    if (ImGui::CollapsingHeader("Character", ImGuiTreeNodeFlags_DefaultOpen)) {
        Character& p = world.player;
        ImGui::Checkbox("Follow camera (F2)", &follow);
        ImGui::SameLine();
        if (ImGui::Button("Respawn")) p.spawn(world.spawnPoint, world.spawnYaw);
        ImGui::Text("Speed %.2f m/s  %s", p.speed(), p.grounded() ? "grounded" : "airborne");
        ImGui::ProgressBar(p.weightIdle, ImVec2(80, 0), "idle");
        ImGui::SameLine();
        ImGui::ProgressBar(p.weightWalk, ImVec2(80, 0), "walk");
        ImGui::SameLine();
        ImGui::ProgressBar(p.weightRun, ImVec2(80, 0), "run");
        ImGui::SameLine();
        ImGui::ProgressBar(p.weightAir, ImVec2(80, 0), "air");
        ImGui::Checkbox("Foot IK", &p.tuning.footIK);
        ImGui::SameLine();
        ImGui::Text("pelvis %.3f m", p.pelvisOffset);
        ImGui::Checkbox("Show skeleton", &r.settings.showSkeleton);
        ImGui::SliderFloat("Walk speed", &p.tuning.walkSpeed, 0.5f, 3.0f);
        ImGui::SliderFloat("Run speed", &p.tuning.runSpeed, 3.0f, 9.0f);
        ImGui::SliderFloat("Walk stride", &p.tuning.walkStride, 0.8f, 2.5f);
        ImGui::SliderFloat("Run stride", &p.tuning.runStride, 1.5f, 4.0f);
        ImGui::TextDisabled("WASD move | Shift run | Space jump");
    }
    if (ImGui::CollapsingHeader("Camera")) {
        ImGui::Text("Target %.1f %.1f %.1f", cam.target.x, cam.target.y, cam.target.z);
        ImGui::Text("yaw %.2f pitch %.2f dist %.1f", cam.yaw, cam.pitch, cam.distance);
        ImGui::TextDisabled("Drag: orbit | Middle: pan | Wheel: zoom");
        ImGui::TextDisabled("WASD/QE: move | Shift: fast");
    }
    ImGui::TextDisabled("F1 UI  F5 reload shaders  F12 screenshot");
    ImGui::Checkbox("ImGui demo", &showDemo);
    ImGui::End();
}

// Renders a fixed set of camera views and prints average per-pass GPU time for each,
// so optimisations can be compared like for like.
void runBenchmark(World& world, Renderer& renderer, const Options& opt) {
    struct View {
        const char* name;
        glm::vec3 target;
        float yaw, pitch, distance;
    };
    const glm::vec3 spawn = world.spawnPoint + glm::vec3(0, 1.5f, 0);
    const View views[] = {
        {"Overview", {-110, 4, 40}, -1.9f, 0.30f, 140.0f},
        {"Ground level", spawn, world.spawnYaw, 0.06f, 6.0f},
        {"Top-down", {0, 0, 0}, 0.4f, 1.25f, 420.0f},
        {"Forest", {-95, 0, 30}, 2.6f, 0.18f, 18.0f},
    };
    const int warmup = 30, measured = 120;
    struct Result {
        std::vector<GpuProfiler::Stat> passes;
        double gpuTotal, cpuMs;
        int drawCalls;
        long long tris;
    };
    std::vector<Result> results;

    for (const auto& v : views) {
        Camera cam;
        cam.target = v.target;
        if (v.target.y == 0.0f) cam.target.y = std::max(world.terrain.heightAt(v.target.x, v.target.z), 0.0f) + 1.5f;
        cam.yaw = v.yaw;
        cam.pitch = v.pitch;
        cam.distance = v.distance;
        keepCameraAboveGround(cam, world, 0.6f);

        auto frame = [&]() {
            glfwPollEvents();
            world.player.update(1.0f / 60.0f, CharacterInput{}, world.terrain);
            world.update(1.0f / 60.0f);
            renderer.render(world, cam);
        };
        // GPU timings: keep the pipeline full (no stalls, so the iGPU holds its clocks).
        for (int f = 0; f < warmup + measured; ++f) {
            if (f == warmup) renderer.profiler.resetStats();
            frame();
        }
        renderer.profiler.resolveAll();
        Result r;
        r.passes = renderer.profiler.stats();
        r.gpuTotal = 0.0;
        for (const auto& p : r.passes) r.gpuTotal += p.avg();
        // CPU submission cost: drain the queue first so we don't time GPU back-pressure.
        double cpu = 0.0;
        const int cpuFrames = 30;
        for (int f = 0; f < cpuFrames; ++f) {
            glFinish();
            auto t0 = std::chrono::steady_clock::now();
            frame();
            cpu += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        }
        r.cpuMs = cpu / cpuFrames;
        r.drawCalls = renderer.drawCalls;
        r.tris = renderer.trianglesDrawn;
        results.push_back(r);
        if (!opt.screenshot.empty()) {
            std::string name = opt.screenshot;
            auto dot = name.rfind('.');
            name = name.substr(0, dot) + "_" + std::to_string(results.size()) + ".png";
            saveScreenshot(renderer.finalFramebuffer(), renderer.width(), renderer.height(), std::filesystem::u8path(name));
        }
    }

    std::printf("\n| Pass (ms) |");
    for (const auto& v : views) std::printf(" %s |", v.name);
    std::printf("\n|---|");
    for (size_t i = 0; i < std::size(views); ++i) std::printf("---|");
    std::printf("\n");
    for (const auto& p : results[0].passes) {
        std::printf("| %s |", p.name.c_str());
        for (const auto& r : results) {
            double ms = 0.0;
            for (const auto& q : r.passes)
                if (q.name == p.name) ms = q.avg();
            std::printf(" %.2f |", ms);
        }
        std::printf("\n");
    }
    std::printf("| **GPU total** |");
    for (const auto& r : results) std::printf(" **%.2f** |", r.gpuTotal);
    std::printf("\n| CPU submit |");
    for (const auto& r : results) std::printf(" %.2f |", r.cpuMs);
    std::printf("\n| Draw calls |");
    for (const auto& r : results) std::printf(" %d |", r.drawCalls);
    std::printf("\n| Triangles |");
    for (const auto& r : results) std::printf(" %.2fM |", double(r.tris) / 1e6);
    std::printf("\n");
}

} // namespace

int main(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);
    const bool offscreen = !opt.screenshot.empty() || opt.bench;

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
    if (offscreen) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(opt.width, opt.height, "IsleRender - deferred PBR island", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "Could not create an OpenGL 4.5 core context\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(offscreen ? 0 : 1);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::fprintf(stderr, "glewInit failed\n");
        return 1;
    }
    glGetError();  // GLEW may leave a benign error behind
    installDebugCallback();
    const std::string gpuName = std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER))) + " / GL " +
                                reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::printf("[gl] %s\n[assets] %s\n", gpuName.c_str(), assetRoot().u8string().c_str());

    glfwSetScrollCallback(win, scrollCallback);
    glfwSetFramebufferSizeCallback(win, resizeCallback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 6.0f;
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.88f;
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 450");

    {  // GL resources live in this scope so they are released while the context is current
    World world;
    world.init(1337);
    if (opt.timeOfDay >= 0.0f) world.timeOfDay = opt.timeOfDay;

    int fbw, fbh;
    glfwGetFramebufferSize(win, &fbw, &fbh);
    if (offscreen) { fbw = opt.width; fbh = opt.height; }
    Renderer renderer;
    if (!renderer.init(fbw, fbh)) {
        std::fprintf(stderr, "Renderer init failed (see shader errors above)\n");
        return 1;
    }
    renderer.settings.debugView = DebugView(std::clamp(opt.debugView, 0, int(DebugView::Count) - 1));
    renderer.settings.wireframe = opt.wireframe;
    renderer.settings.showSkeleton = opt.skeleton;
    world.player.tuning.footIK = !opt.noIK;
    world.terrainLod.enableLod = world.terrainLod.enableCulling = !opt.noLod;
    renderer.props().cullingEnabled = !opt.noCull;
    if (opt.gpuCull) renderer.props().mode = PropRenderer::Mode::Gpu;
    renderer.settings.proceduralWater = opt.oldWater;
    world.terrainLod.useMultiDraw = opt.multiDraw;
    if (opt.bench) {
        runBenchmark(world, renderer, opt);
        glfwSetWindowShouldClose(win, 1);  // skip the interactive loop
    }

    if (!std::isnan(opt.spawnX)) {
        world.spawnPoint = {opt.spawnX, world.terrain.heightAt(opt.spawnX, opt.spawnZ), opt.spawnZ};
        world.spawnYaw = opt.spawnYaw;
        world.player.spawn(world.spawnPoint, world.spawnYaw);
    }

    bool follow = !opt.freeCamera;
    Camera cam;
    if (follow) {
        cam.target = world.player.position() + glm::vec3(0, 1.3f, 0);
        cam.distance = 7.0f;
        cam.pitch = 0.22f;
        cam.yaw = world.spawnYaw + 3.14159265f;  // behind the player
    } else {
        if (!std::isnan(opt.targetX)) cam.target = {opt.targetX, 0.0f, opt.targetZ};
        cam.target.y = std::max(world.terrain.heightAt(cam.target.x, cam.target.z), 0.0f) + 4.0f;
    }
    if (!std::isnan(opt.yaw)) cam.yaw = opt.yaw;
    if (!std::isnan(opt.pitch)) cam.pitch = opt.pitch;
    if (!std::isnan(opt.distance)) cam.distance = opt.distance;

    bool showUI = true, showDemo = false;
    bool f1 = false, f2 = false, f5 = false, f12 = false;
    int shotCounter = 0, frame = 0;
    double cpuAccum = 0.0;
    float cpuMs = 0.0f, reloadTimer = 0.0f;
    auto last = std::chrono::steady_clock::now();
    glfwGetCursorPos(win, &g_input.lastX, &g_input.lastY);

    while (!glfwWindowShouldClose(win)) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (offscreen) dt = 1.0f / 60.0f;  // deterministic animation for screenshots
        dt = std::min(dt, 0.1f);

        glfwPollEvents();
        if (!offscreen && g_input.resized) {
            glfwGetFramebufferSize(win, &fbw, &fbh);
            if (fbw > 0 && fbh > 0) renderer.resize(fbw, fbh);
            g_input.resized = false;
        }
        if (fbw == 0 || fbh == 0) { glfwWaitEvents(); continue; }

        auto edge = [&](int key, bool& state) {
            bool down = glfwGetKey(win, key) == GLFW_PRESS;
            bool pressed = down && !state;
            state = down;
            return pressed;
        };
        if (edge(GLFW_KEY_F1, f1)) showUI = !showUI;
        if (edge(GLFW_KEY_F2, f2)) follow = !follow;
        if (edge(GLFW_KEY_F5, f5)) std::printf("[shader] reload all: %s\n", renderer.reloadAll() ? "ok" : "failed");
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(win, 1);
        reloadTimer += dt;
        if (!offscreen && reloadTimer > 0.5f) { renderer.hotReload(); reloadTimer = 0.0f; }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        auto cpuStart = std::chrono::steady_clock::now();
        // ---- player input -> character -> follow camera
        CharacterInput ci;
        ci.cameraYaw = cam.yaw;
        if (follow) {
            if (offscreen) {
                if (!std::isnan(opt.moveDir)) ci.move = {std::sin(opt.moveDir), std::cos(opt.moveDir)};
                ci.run = opt.run;
            } else if (!ImGui::GetIO().WantCaptureKeyboard) {
                auto key = [&](int k) { return glfwGetKey(win, k) == GLFW_PRESS ? 1.0f : 0.0f; };
                ci.move = {key(GLFW_KEY_D) - key(GLFW_KEY_A), key(GLFW_KEY_W) - key(GLFW_KEY_S)};
                ci.run = key(GLFW_KEY_LEFT_SHIFT) > 0.0f;
                ci.jump = key(GLFW_KEY_SPACE) > 0.0f;
            }
        }
        world.player.update(dt, ci, world.terrain);
        if (follow) {
            glm::vec3 want = world.player.position() + glm::vec3(0, 1.3f, 0);
            cam.target = offscreen ? want : glm::mix(cam.target, want, 1.0f - std::exp(-12.0f * dt));
        }
        if (!offscreen) updateCamera(win, cam, world, dt, !follow);
        keepCameraAboveGround(cam, world, follow ? 0.6f : 1.5f);
        world.update(dt);
        renderer.render(world, cam);

        glBlitNamedFramebuffer(renderer.finalFramebuffer(), 0, 0, 0, renderer.width(), renderer.height(), 0, 0, fbw, fbh,
                               GL_COLOR_BUFFER_BIT, GL_NEAREST);
        float cpuFrame = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - cpuStart).count();
        cpuMs = cpuMs * 0.9f + cpuFrame * 0.1f;

        if (edge(GLFW_KEY_F12, f12)) {
            char name[64];
            std::snprintf(name, sizeof(name), "screenshot_%03d.png", shotCounter++);
            saveScreenshot(renderer.finalFramebuffer(), renderer.width(), renderer.height(), name);
            std::printf("[shot] saved %s\n", name);
        }

        if (showUI && !offscreen) {
            drawUI(renderer, world, cam, cpuMs, gpuName.c_str(), showDemo, follow);
            if (showDemo) ImGui::ShowDemoWindow(&showDemo);
        }
        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);

        ++frame;
        if (offscreen) {
            if (frame > opt.frames / 2) cpuAccum += cpuFrame;
            if (frame >= opt.frames) {
                renderer.profiler.resolveAll();
                bool ok = saveScreenshot(renderer.finalFramebuffer(), renderer.width(), renderer.height(),
                                         std::filesystem::u8path(opt.screenshot));
                std::printf("[shot] %s -> %s (%dx%d)\n", ok ? "saved" : "FAILED", opt.screenshot.c_str(),
                            renderer.width(), renderer.height());
                glm::vec3 eye = cam.position(), pp = world.player.position();
                std::printf("[cam] eye %.2f %.2f %.2f  target %.2f %.2f %.2f  player %.2f %.2f %.2f yaw %.2f\n", eye.x,
                            eye.y, eye.z, cam.target.x, cam.target.y, cam.target.z, pp.x, pp.y, pp.z,
                            world.player.yaw());
                std::printf("[perf] %d draw calls, %.2fM triangles, CPU submit %.2f ms\n", renderer.drawCalls,
                            double(renderer.trianglesDrawn) / 1e6, cpuAccum / double(opt.frames - opt.frames / 2));
                for (const auto& e : renderer.profiler.entries())
                    std::printf("[perf] %-16s %6.2f ms\n", e.name.c_str(), e.ms);
                std::printf("[perf] %-16s %6.2f ms\n", "GPU total", renderer.profiler.totalMs());
                break;
            }
        }
    }
    }  // end GL resource scope

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
