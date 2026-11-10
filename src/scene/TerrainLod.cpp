#include "scene/TerrainLod.h"

#include "scene/Terrain.h"

#include <algorithm>
#include <cmath>

namespace isle {

void TerrainLod::build(const Terrain& terrain, int tileCells) {
    MeshData grid = terrain.buildMesh();  // (res x res) vertices, row-major
    const int res = int(std::lround(std::sqrt(double(grid.vertices.size()))));
    const int cells = res - 1;
    const int tilesPerSide = cells / tileCells;

    MeshData out;
    out.vertices = grid.vertices;
    for (auto& v : out.vertices) v.color = glm::vec3(0.0f);  // color.x = skirt drop (0 on the surface)
    auto vid = [&](int i, int j) { return uint32_t(j * res + i); };

    tiles_.clear();
    for (int ty = 0; ty < tilesPerSide; ++ty) {
        for (int tx = 0; tx < tilesPerSide; ++tx) {
            Tile tile{};
            const int i0 = tx * tileCells, j0 = ty * tileCells;
            float hMin = 1e9f, hMax = -1e9f;
            for (int j = j0; j <= j0 + tileCells; ++j)
                for (int i = i0; i <= i0 + tileCells; ++i) {
                    float h = grid.vertices[vid(i, j)].pos.y;
                    hMin = std::min(hMin, h);
                    hMax = std::max(hMax, h);
                }

            for (int l = 0; l < kLods; ++l) {
                const int step = 1 << l;
                tile.lod[l].first = uint32_t(out.indices.size());
                for (int j = j0; j < j0 + tileCells; j += step)
                    for (int i = i0; i < i0 + tileCells; i += step) {
                        uint32_t a = vid(i, j), b = vid(i + step, j), c = vid(i, j + step), d = vid(i + step, j + step);
                        out.indices.insert(out.indices.end(), {a, c, b, b, c, d});
                    }
                // Skirts: a vertical strip hanging below each tile edge hides T-junction cracks.
                const float depth = 1.0f + 1.5f * float(step);
                auto skirtEdge = [&](int ia, int ja, int di, int dj) {
                    for (int k = 0; k < tileCells; k += step) {
                        uint32_t a = vid(ia + di * k, ja + dj * k);
                        uint32_t b = vid(ia + di * (k + step), ja + dj * (k + step));
                        uint32_t base = uint32_t(out.vertices.size());
                        for (uint32_t src : {a, b}) {
                            Vertex v = out.vertices[src];
                            v.pos.y -= depth;
                            v.color = glm::vec3(depth, 0.0f, 0.0f);
                            out.vertices.push_back(v);
                        }
                        out.indices.insert(out.indices.end(), {a, b, base + 1, a, base + 1, base});
                    }
                };
                skirtEdge(i0, j0, 1, 0);
                skirtEdge(i0, j0 + tileCells, 1, 0);
                skirtEdge(i0, j0, 0, 1);
                skirtEdge(i0 + tileCells, j0, 0, 1);
                tile.lod[l].count = uint32_t(out.indices.size()) - tile.lod[l].first;
                {
                    auto b = out.indices.begin() + tile.lod[l].first;
                    auto mm = std::minmax_element(b, out.indices.end());
                    tile.lod[l].minVertex = *mm.first;
                    tile.lod[l].maxVertex = *mm.second;
                }
                if (l == kLods - 1) hMin -= depth;  // bounds include the deepest skirt
            }
            const glm::vec3 p0 = grid.vertices[vid(i0, j0)].pos;
            const glm::vec3 p1 = grid.vertices[vid(i0 + tileCells, j0 + tileCells)].pos;
            tile.boundsMin = {p0.x, hMin, p0.z};
            tile.boundsMax = {p1.x, hMax, p1.z};
            tiles_.push_back(tile);
        }
    }
    mesh_.upload(out);
}

void TerrainLod::select(const Frustum& frustum, const glm::vec3& origin, Selection& out) const {
    out.counts.clear();
    out.offsets.clear();
    out.minVertex.clear();
    out.maxVertex.clear();
    out.triangles = 0;
    out.tiles = 0;
    for (const Tile& t : tiles_) {
        if (enableCulling && !frustum.intersectsAABB(t.boundsMin, t.boundsMax)) continue;
        int lod = 0;
        if (enableLod) {
            glm::vec3 nearest = glm::clamp(origin, t.boundsMin, t.boundsMax);
            float d = glm::length(nearest - origin);
            while (lod < kLods - 1 && d > lodDistance * float(1 << lod)) ++lod;
        }
        out.counts.push_back(GLsizei(t.lod[lod].count));
        out.offsets.push_back(reinterpret_cast<const void*>(uintptr_t(t.lod[lod].first) * sizeof(uint32_t)));
        out.minVertex.push_back(t.lod[lod].minVertex);
        out.maxVertex.push_back(t.lod[lod].maxVertex);
        out.triangles += t.lod[lod].count / 3;
        ++out.tiles;
    }
}

void TerrainLod::draw(const Selection& sel) const {
    if (sel.counts.empty()) return;
    if (useMultiDraw) {
        mesh_.drawMulti(sel.counts.data(), sel.offsets.data(), GLsizei(sel.counts.size()));
        return;
    }
    for (size_t i = 0; i < sel.counts.size(); ++i)
        mesh_.drawRange(sel.minVertex[i], sel.maxVertex[i], sel.counts[i], sel.offsets[i]);
}

} // namespace isle
