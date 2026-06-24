// LumenSlice — marching cubes over the segmentation mask.
//
// Turns a binary label mask into a triangle surface. The mask is read as a 0/1
// scalar field at isolevel 0.5; `smooth_iters` of a light blur on that field
// soften the voxel stair-steps, and `downsample` (>=1) samples the field on a
// coarser grid to cap triangle counts on large masks. Vertices are emitted in
// physical millimetres (using the volume spacing) and welded by edge identity so
// the surface is a proper indexed mesh with smooth per-vertex normals.

#pragma once

#include <cstdint>
#include <vector>

namespace lumen {

struct Mesh {
    std::vector<float> vertices;        // 3 floats per vertex (x,y,z, mm)
    std::vector<float> normals;         // 3 floats per vertex (unit)
    std::vector<std::uint32_t> indices; // 3 per triangle

    void clear() {
        vertices.clear();
        normals.clear();
        indices.clear();
    }
    [[nodiscard]] int vertex_count() const {
        return static_cast<int>(vertices.size() / 3);
    }
    [[nodiscard]] int triangle_count() const {
        return static_cast<int>(indices.size() / 3);
    }
};

// Run marching cubes on `mask` (dims w*h*d, value != 0 = inside). `spacing_*` are
// the mm voxel sizes. `smooth_iters` >= 0; `downsample` >= 1. Result replaces
// `out`. Returns the triangle count.
int marching_cubes(const std::uint8_t* mask, int w, int h, int d, float spacing_x,
                   float spacing_y, float spacing_z, int smooth_iters,
                   int downsample, Mesh& out);

} // namespace lumen
