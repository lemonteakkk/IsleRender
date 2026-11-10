#include "scene/Procedural.h"

#include "scene/Noise.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace isle {

namespace {

constexpr float kPi = 3.14159265358979f;

void addTri(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 color, float emissive = 0.0f) {
    glm::vec3 n = glm::cross(b - a, c - a);
    float len = glm::length(n);
    n = len > 1e-8f ? n / len : glm::vec3(0, 1, 0);
    uint32_t base = uint32_t(m.vertices.size());
    for (const auto& p : {a, b, c}) m.vertices.push_back({p, n, {emissive, 0.0f}, color});
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2});
}

void addQuad(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 color, float e = 0.0f) {
    addTri(m, a, b, c, color, e);
    addTri(m, a, c, d, color, e);
}

// Frustum of a cone (radiusTop may be 0 for a cone) along +Y.
void addCylinder(MeshData& m, glm::vec3 base, float rBottom, float rTop, float height, int seg, glm::vec3 color,
                 float twist = 0.0f) {
    for (int i = 0; i < seg; ++i) {
        float a0 = 2 * kPi * float(i) / float(seg), a1 = 2 * kPi * float(i + 1) / float(seg);
        glm::vec3 b0 = base + glm::vec3(std::cos(a0), 0, std::sin(a0)) * rBottom;
        glm::vec3 b1 = base + glm::vec3(std::cos(a1), 0, std::sin(a1)) * rBottom;
        glm::vec3 top = base + glm::vec3(0, height, 0);
        glm::vec3 t0 = top + glm::vec3(std::cos(a0 + twist), 0, std::sin(a0 + twist)) * rTop;
        glm::vec3 t1 = top + glm::vec3(std::cos(a1 + twist), 0, std::sin(a1 + twist)) * rTop;
        if (rTop <= 0.0f) {
            addTri(m, b0, top, b1, color);
        } else {
            addQuad(m, b0, t0, t1, b1, color);
            addTri(m, top, t1, t0, color);
        }
        addTri(m, base, b0, b1, color);  // bottom cap
    }
}

void addBox(MeshData& m, glm::vec3 c, glm::vec3 h, glm::vec3 color, float e = 0.0f) {
    glm::vec3 p[8];
    for (int i = 0; i < 8; ++i)
        p[i] = c + glm::vec3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
    addQuad(m, p[0], p[2], p[3], p[1], color, e);  // -z
    addQuad(m, p[4], p[5], p[7], p[6], color, e);  // +z
    addQuad(m, p[0], p[4], p[6], p[2], color, e);  // -x
    addQuad(m, p[1], p[3], p[7], p[5], color, e);  // +x
    addQuad(m, p[2], p[6], p[7], p[3], color, e);  // +y
    addQuad(m, p[0], p[1], p[5], p[4], color, e);  // -y
}

// Lumpy subdivided icosahedron.
void addBlob(MeshData& m, glm::vec3 center, glm::vec3 radius, int subdiv, float jitter, uint32_t seed,
             glm::vec3 color) {
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    std::vector<glm::vec3> v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                                {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (auto& x : v) x = glm::normalize(x);
    std::vector<std::array<int, 3>> f = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
                                         {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                         {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
                                         {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1}};
    for (int s = 0; s < subdiv; ++s) {
        std::map<std::pair<int, int>, int> mid;
        auto midpoint = [&](int a, int b) {
            auto key = std::minmax(a, b);
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            v.push_back(glm::normalize(v[a] + v[b]));
            return mid[key] = int(v.size()) - 1;
        };
        std::vector<std::array<int, 3>> nf;
        for (auto& tri : f) {
            int a = midpoint(tri[0], tri[1]), b = midpoint(tri[1], tri[2]), c = midpoint(tri[2], tri[0]);
            nf.push_back({tri[0], a, c});
            nf.push_back({tri[1], b, a});
            nf.push_back({tri[2], c, b});
            nf.push_back({a, b, c});
        }
        f.swap(nf);
    }
    std::vector<glm::vec3> p(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        float n = valueNoise(v[i].x * 2.3f + v[i].z * 1.7f + 5.0f, v[i].y * 2.3f - v[i].z * 1.3f, seed);
        p[i] = center + v[i] * radius * (1.0f + (n - 0.5f) * 2.0f * jitter);
    }
    for (auto& tri : f) addTri(m, p[tri[0]], p[tri[1]], p[tri[2]], color);
}

glm::vec3 srgb(float r, float g, float b) { return glm::pow(glm::vec3(r, g, b), glm::vec3(2.2f)); }

} // namespace

MeshData makePineTree() {
    MeshData m;
    glm::vec3 bark = srgb(0.36f, 0.24f, 0.16f);
    glm::vec3 needle = srgb(0.16f, 0.36f, 0.20f);
    addCylinder(m, {0, -0.5f, 0}, 0.35f, 0.25f, 3.0f, 6, bark);
    float y = 1.8f, r = 2.6f, h = 4.0f;
    for (int i = 0; i < 3; ++i) {
        addCylinder(m, {0, y, 0}, r, 0.0f, h, 7, needle * (1.0f + 0.12f * float(i)), 0.3f * float(i));
        y += h * 0.55f;
        r *= 0.72f;
        h *= 0.82f;
    }
    return m;
}

MeshData makeRoundTree() {
    MeshData m;
    glm::vec3 bark = srgb(0.40f, 0.28f, 0.18f);
    glm::vec3 leaf = srgb(0.36f, 0.55f, 0.20f);
    addCylinder(m, {0, -0.5f, 0}, 0.4f, 0.28f, 4.2f, 6, bark);
    addBlob(m, {0, 5.4f, 0}, {2.6f, 2.2f, 2.6f}, 1, 0.22f, 11, leaf);
    addBlob(m, {1.4f, 4.4f, 0.6f}, {1.6f, 1.4f, 1.6f}, 1, 0.25f, 23, leaf * 0.9f);
    addBlob(m, {-1.2f, 4.6f, -0.8f}, {1.7f, 1.5f, 1.7f}, 1, 0.25f, 37, leaf * 1.08f);
    return m;
}

MeshData makeRock(uint32_t seed) {
    MeshData m;
    addBlob(m, {0, 0.3f, 0}, {1.4f, 0.9f, 1.1f}, 1, 0.35f, seed, srgb(0.48f, 0.46f, 0.43f));
    return m;
}

MeshData makeLantern() {
    MeshData m;
    glm::vec3 wood = srgb(0.30f, 0.20f, 0.13f);
    glm::vec3 glow = srgb(1.0f, 0.72f, 0.38f);
    addBox(m, {0, 1.2f, 0}, {0.08f, 1.3f, 0.08f}, wood);
    addBox(m, {0, 2.6f, 0}, {0.26f, 0.06f, 0.26f}, wood);
    addBox(m, {0, 2.32f, 0}, {0.18f, 0.22f, 0.18f}, glow, 1.0f);
    addCylinder(m, {0, 2.66f, 0}, 0.32f, 0.0f, 0.28f, 4, wood, kPi / 4);
    return m;
}

MeshData makeGrid(int cells, float size) {
    MeshData m;
    int n = cells + 1;
    m.vertices.reserve(size_t(n) * n);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            Vertex v;
            v.pos = {-size * 0.5f + size * float(i) / float(cells), 0.0f, -size * 0.5f + size * float(j) / float(cells)};
            m.vertices.push_back(v);
        }
    for (int j = 0; j < cells; ++j)
        for (int i = 0; i < cells; ++i) {
            uint32_t a = uint32_t(j * n + i), b = a + 1, c = a + uint32_t(n), d = c + 1;
            m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
        }
    return m;
}

} // namespace isle
