#pragma once
#include "gfx/Mesh.h"
#include "scene/Frustum.h"

#include <vector>

namespace isle {

class Terrain;

// Tiled terrain with discrete distance LODs.
// One shared vertex buffer; per tile and LOD a contiguous index range (grid step 1/2/4/8)
// plus skirts that hide cracks between neighbours at different LODs. Each pass culls tiles
// against its own frustum and draws the survivors with explicit vertex ranges.
class TerrainLod {
public:
    static constexpr int kLods = 4;

    void build(const Terrain& terrain, int tileCells = 32);

    struct Selection {
        std::vector<GLsizei> counts;
        std::vector<const void*> offsets;
        std::vector<GLuint> minVertex, maxVertex;  // explicit ranges for glDrawRangeElements
        long long triangles = 0;
        int tiles = 0;
    };
    // LOD is chosen from distance to `lodOrigin` (the camera) so every pass agrees on geometry.
    void select(const Frustum& frustum, const glm::vec3& lodOrigin, Selection& out) const;
    void draw(const Selection& sel) const;

    int tileCount() const { return int(tiles_.size()); }
    float lodDistance = 75.0f;  // LOD n is used beyond lodDistance * 2^(n-1)
    bool enableLod = true;
    bool enableCulling = true;
    // glMultiDrawElements made the Intel driver burn CPU time proportional to the index count
    // (it derives vertex ranges itself); per-tile glDrawRangeElements passes them explicitly.
    bool useMultiDraw = false;

private:
    struct Range {
        uint32_t first, count;
        uint32_t minVertex, maxVertex;
    };
    struct Tile {
        glm::vec3 boundsMin, boundsMax;
        Range lod[kLods];
    };
    std::vector<Tile> tiles_;
    Mesh mesh_;
};

} // namespace isle
