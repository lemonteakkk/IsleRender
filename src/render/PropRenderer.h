#pragma once
#include "gfx/GL.h"
#include "gfx/Mesh.h"
#include "gfx/Shader.h"
#include "scene/Frustum.h"

#include <array>
#include <vector>

namespace isle {

class World;

// GPU-driven rendering of all static props (trees, rocks, lanterns).
//  - every prop mesh is merged into one vertex/index buffer (roughness baked per vertex)
//  - a compute shader frustum-culls every instance for every pass (3 shadow cascades +
//    camera, plus screen-size culling for the camera) and appends survivors with atomics
//    straight into DrawElementsIndirectCommand.instanceCount
//  - each pass then issues a single glMultiDrawElementsIndirect
//  - command/output buffers are triple-buffered and fenced so the CPU never overwrites
//    data the GPU is still reading
class PropRenderer {
public:
    static constexpr int kPasses = 4;  // cascades 0..2, camera = 3
    static constexpr int kCameraPass = 3;
    static constexpr int kRing = 3;

    ~PropRenderer();
    bool init(const World& world);
    // Waits until this frame's ring slot is free again (call outside GPU timer scopes).
    void beginFrame();
    // Records the culling dispatches for all passes of this frame.
    void cull(const Frustum frustums[kPasses], const glm::vec3& cameraPos, float pixelThreshold, float projScale);
    void draw(int pass) const;
    void endFrame();

    Shader& shader() { return cull_; }
    bool cullingEnabled = true;
    // Gpu: compute culling + glMultiDrawElementsIndirect.
    // Cpu: CPU culling into a persistently mapped ring + glDrawElementsInstancedBaseInstance.
    enum class Mode { Gpu, Cpu };
    Mode mode = Mode::Cpu;  // measured faster on Intel Iris Xe: its MDI with GPU-written args stalls
    int drawCallsPerPass() const;

    int instanceCount() const { return int(instances_.size()); }
    int batchCount() const { return int(batches_.size()); }
    // Visible instances / triangles per pass, read back from a frame that has finished.
    int visibleInstances[kPasses] = {};
    long long visibleTriangles[kPasses] = {};

private:
    struct Batch {
        uint32_t firstIndex, indexCount, instanceBase, instanceCount;
    };
    struct GpuInstance {
        glm::mat4 model;
        glm::vec4 sphere;      // world-space bounding sphere
        glm::uvec4 info;       // x = batch, y = output offset of the batch within a pass
    };
    struct Command {
        GLuint count, instanceCount, firstIndex, baseVertex, baseInstance;
    };

    size_t commandOffset(int slot, int pass) const {
        return (size_t(slot) * kPasses + size_t(pass)) * batches_.size() * sizeof(Command);
    }
    void readBackStats(int slot);
    void bindForMode();
    void cullCpu(const Frustum frustums[kPasses], const glm::vec3& cameraPos, float minRatio);
    GLuint cpuRing_ = 0;
    glm::mat4* cpuMapped_ = nullptr;
    std::vector<uint32_t> cpuCounts_;
    int boundMode_ = -1;

    Mesh mesh_;
    Shader cull_;
    std::vector<Batch> batches_;
    std::vector<GpuInstance> instances_;
    GLuint instanceBuf_ = 0, commandBuf_ = 0, visibleBuf_ = 0;
    GLsync fences_[kRing] = {};
    int slot_ = 0;
    unsigned frame_ = 0;
};

} // namespace isle
