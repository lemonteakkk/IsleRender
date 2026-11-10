#include "render/PropRenderer.h"

#include "scene/World.h"

#include <algorithm>
#include <cstdio>

namespace isle {

PropRenderer::~PropRenderer() {
    for (auto& f : fences_)
        if (f) glDeleteSync(f);
    if (cpuMapped_) glUnmapNamedBuffer(cpuRing_);
    GLuint bufs[] = {instanceBuf_, commandBuf_, visibleBuf_, cpuRing_};
    glDeleteBuffers(4, bufs);
}

bool PropRenderer::init(const World& world) {
    if (!cull_.loadCompute("cull.comp")) return false;

    MeshData merged;
    for (size_t bi = 0; bi < world.props.size(); ++bi) {
        const PropBatch& pb = world.props[bi];
        Batch b{};
        b.firstIndex = uint32_t(merged.indices.size());
        b.indexCount = uint32_t(pb.data.indices.size());
        b.instanceBase = uint32_t(instances_.size());
        b.instanceCount = uint32_t(pb.instances.size());

        const uint32_t baseVertex = uint32_t(merged.vertices.size());
        glm::vec3 mn(1e9f), mx(-1e9f);
        for (Vertex v : pb.data.vertices) {
            v.uv.y = pb.roughness;  // per-vertex roughness lets all batches share one draw
            merged.vertices.push_back(v);
            mn = glm::min(mn, v.pos);
            mx = glm::max(mx, v.pos);
        }
        for (uint32_t i : pb.data.indices) merged.indices.push_back(i + baseVertex);

        const glm::vec3 center = (mn + mx) * 0.5f;
        float radius = 0.0f;
        for (const Vertex& v : pb.data.vertices) radius = std::max(radius, glm::length(v.pos - center));

        for (const glm::mat4& m : pb.instances) {
            float scale = std::max({glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])),
                                    glm::length(glm::vec3(m[2]))});
            GpuInstance gi;
            gi.model = m;
            gi.sphere = glm::vec4(glm::vec3(m * glm::vec4(center, 1.0f)), radius * scale);
            gi.info = glm::uvec4(uint32_t(bi), b.instanceBase, 0u, 0u);
            instances_.push_back(gi);
        }
        batches_.push_back(b);
    }
    mesh_.upload(merged);

    const size_t n = instances_.size();
    glCreateBuffers(1, &instanceBuf_);
    glNamedBufferStorage(instanceBuf_, GLsizeiptr(n * sizeof(GpuInstance)), instances_.data(), 0);
    glCreateBuffers(1, &visibleBuf_);
    glNamedBufferStorage(visibleBuf_, GLsizeiptr(size_t(kRing) * kPasses * n * sizeof(glm::mat4)), nullptr, 0);
    glCreateBuffers(1, &commandBuf_);
    glNamedBufferStorage(commandBuf_, GLsizeiptr(commandOffset(kRing, 0)), nullptr, GL_DYNAMIC_STORAGE_BIT);
    // CPU path: persistently mapped, coherent ring that culled matrices are written into directly.
    glCreateBuffers(1, &cpuRing_);
    const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    glNamedBufferStorage(cpuRing_, GLsizeiptr(size_t(kRing) * kPasses * n * sizeof(glm::mat4)), nullptr, flags);
    cpuMapped_ = static_cast<glm::mat4*>(
        glMapNamedBufferRange(cpuRing_, 0, GLsizeiptr(size_t(kRing) * kPasses * n * sizeof(glm::mat4)), flags));
    cpuCounts_.assign(size_t(kRing) * kPasses * batches_.size(), 0u);

    std::printf("[props] %zu instances in %zu batches\n", n, batches_.size());
    return cpuMapped_ != nullptr;
}

void PropRenderer::bindForMode() {
    if (boundMode_ == int(mode)) return;
    mesh_.bindInstanceBuffer(mode == Mode::Gpu ? visibleBuf_ : cpuRing_);
    boundMode_ = int(mode);
}

void PropRenderer::cullCpu(const Frustum frustums[kPasses], const glm::vec3& cameraPos, float minRatio) {
    const size_t n = instances_.size(), nb = batches_.size();
    for (int p = 0; p < kPasses; ++p) {
        const size_t passBase = (size_t(slot_) * kPasses + size_t(p)) * n;
        uint32_t* counts = &cpuCounts_[(size_t(slot_) * kPasses + size_t(p)) * nb];
        std::fill(counts, counts + nb, 0u);
        for (const GpuInstance& gi : instances_) {
            const glm::vec3 c(gi.sphere);
            if (cullingEnabled) {
                if (!frustums[p].intersectsSphere(c, gi.sphere.w)) continue;
                if (p == kCameraPass && gi.sphere.w / std::max(glm::length(c - cameraPos), 1e-3f) < minRatio) continue;
            }
            const uint32_t b = gi.info.x;
            cpuMapped_[passBase + gi.info.y + counts[b]++] = gi.model;
        }
        visibleInstances[p] = 0;
        visibleTriangles[p] = 0;
        for (size_t b = 0; b < nb; ++b) {
            visibleInstances[p] += int(counts[b]);
            visibleTriangles[p] += (long long)counts[b] * (batches_[b].indexCount / 3);
        }
    }
}

void PropRenderer::readBackStats(int slot) {
    std::vector<Command> cmds(size_t(kPasses) * batches_.size());
    glGetNamedBufferSubData(commandBuf_, GLintptr(commandOffset(slot, 0)), GLsizeiptr(cmds.size() * sizeof(Command)),
                            cmds.data());
    for (int p = 0; p < kPasses; ++p) {
        visibleInstances[p] = 0;
        visibleTriangles[p] = 0;
        for (size_t b = 0; b < batches_.size(); ++b) {
            const Command& c = cmds[size_t(p) * batches_.size() + b];
            visibleInstances[p] += int(c.instanceCount);
            visibleTriangles[p] += (long long)c.instanceCount * (c.count / 3);
        }
    }
}

void PropRenderer::beginFrame() {
    // Reuse a ring slot only once the GPU has finished with it (3 frames ago).
    if (fences_[slot_]) {
        glClientWaitSync(fences_[slot_], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        glDeleteSync(fences_[slot_]);
        fences_[slot_] = nullptr;
        if (mode == Mode::Gpu) readBackStats(slot_);
    }
    bindForMode();
}

void PropRenderer::cull(const Frustum frustums[kPasses], const glm::vec3& cameraPos, float pixelThreshold,
                        float projScale) {
    if (mode == Mode::Cpu) {
        cullCpu(frustums, cameraPos, pixelThreshold / std::max(projScale, 1.0f));
        return;
    }

    const size_t n = instances_.size();
    std::vector<Command> cmds;
    cmds.reserve(size_t(kPasses) * batches_.size());
    for (int p = 0; p < kPasses; ++p)
        for (const Batch& b : batches_) {
            GLuint outBase = GLuint((size_t(slot_) * kPasses + size_t(p)) * n);
            cmds.push_back({b.indexCount, 0u, b.firstIndex, 0u, outBase + b.instanceBase});
        }
    glNamedBufferSubData(commandBuf_, GLintptr(commandOffset(slot_, 0)), GLsizeiptr(cmds.size() * sizeof(Command)),
                         cmds.data());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, instanceBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, commandBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, visibleBuf_);
    cull_.setUint("uInstanceCount", unsigned(n));
    cull_.set("uCullEnabled", cullingEnabled ? 1 : 0);
    cull_.set("uCamPos", cameraPos);
    cull_.set("uMinRatio", pixelThreshold / std::max(projScale, 1.0f));
    cull_.use();
    for (int p = 0; p < kPasses; ++p) {
        cull_.setArray("uPlanes", frustums[p].planes, 6);
        cull_.setUint("uCmdBase", unsigned((size_t(slot_) * kPasses + size_t(p)) * batches_.size()));
        cull_.setUint("uOutBase", unsigned((size_t(slot_) * kPasses + size_t(p)) * n));
        cull_.set("uScreenCull", p == kCameraPass ? 1 : 0);
        glDispatchCompute(GLuint((n + 63) / 64), 1, 1);
    }
    // indirect args + instance attributes are consumed by the draws that follow
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
}

void PropRenderer::draw(int pass) const {
    if (mode == Mode::Gpu) {
        mesh_.drawIndirect(commandBuf_, commandOffset(slot_, pass), GLsizei(batches_.size()));
        return;
    }
    const size_t n = instances_.size(), nb = batches_.size();
    const uint32_t* counts = &cpuCounts_[(size_t(slot_) * kPasses + size_t(pass)) * nb];
    const GLuint passBase = GLuint((size_t(slot_) * kPasses + size_t(pass)) * n);
    for (size_t b = 0; b < nb; ++b) {
        if (counts[b] == 0) continue;
        mesh_.drawInstancedRange(batches_[b].firstIndex, batches_[b].indexCount, GLsizei(counts[b]),
                                 passBase + batches_[b].instanceBase);
    }
}

int PropRenderer::drawCallsPerPass() const { return mode == Mode::Gpu ? 1 : int(batches_.size()); }

void PropRenderer::endFrame() {
    fences_[slot_] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    slot_ = (slot_ + 1) % kRing;
    ++frame_;
}

} // namespace isle
