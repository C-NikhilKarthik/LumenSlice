#include "segmentation/segment.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lumen {
namespace {

// 6-connected neighbour offsets.
constexpr std::array<std::array<int, 3>, 6> kNeighbours = {{
    {{1, 0, 0}}, {{-1, 0, 0}}, {{0, 1, 0}},
    {{0, -1, 0}}, {{0, 0, 1}}, {{0, 0, -1}},
}};

} // namespace

void threshold_fill(const Volume& vol, float lo, float hi, LabelVolume& mask) {
    if (!vol.valid() || !mask.valid()) return;
    if (lo > hi) std::swap(lo, hi);

    const float* hu = vol.voxel_buffer.get();
    std::uint8_t* out = mask.data();
    const std::size_t n = mask.voxel_count();
    for (std::size_t i = 0; i < n; ++i) {
        const float v = hu[i];
        out[i] = (v >= lo && v <= hi) ? kActiveLabel : 0;
    }
}

long region_grow(const Volume& vol, int sx, int sy, int sz, float tol,
                 LabelVolume& mask) {
    if (!vol.valid() || !mask.valid()) return 0;
    if (!mask.in_bounds(sx, sy, sz)) return 0;

    const float seed_hu = vol.voxel_buffer[vol.index(sx, sy, sz)];
    const float lo = seed_hu - tol;
    const float hi = seed_hu + tol;

    // The mask doubles as the visited set: a voxel is enqueued exactly once, at
    // which point it is labelled, so it can never be revisited. That bounds the
    // whole fill at voxel_count().
    std::vector<VoxelCoord> stack;
    stack.push_back({sx, sy, sz});
    mask.set(sx, sy, sz, kActiveLabel);
    long added = 1;

    while (!stack.empty()) {
        const VoxelCoord c = stack.back();
        stack.pop_back();
        for (const auto& d : kNeighbours) {
            const int nx = c.x + d[0];
            const int ny = c.y + d[1];
            const int nz = c.z + d[2];
            if (!mask.in_bounds(nx, ny, nz)) continue;
            if (mask.at(nx, ny, nz) != 0) continue; // already labelled/visited
            const float v = vol.voxel_buffer[vol.index(nx, ny, nz)];
            if (v < lo || v > hi) continue;
            mask.set(nx, ny, nz, kActiveLabel);
            stack.push_back({nx, ny, nz});
            ++added;
        }
    }
    return added;
}

long paint_disk(const Volume& vol, Axis axis, int index, int cx, int cy,
                int radius, bool add, LabelVolume& mask) {
    if (!vol.valid() || !mask.valid() || radius < 0) return 0;

    const SliceDims dims = slice_dims(vol, axis);
    const std::uint8_t value = add ? kActiveLabel : 0;
    const int r2 = radius * radius;
    long changed = 0;

    const int px0 = std::max(0, cx - radius);
    const int px1 = std::min(dims.width - 1, cx + radius);
    const int py0 = std::max(0, cy - radius);
    const int py1 = std::min(dims.height - 1, cy + radius);

    for (int py = py0; py <= py1; ++py) {
        for (int px = px0; px <= px1; ++px) {
            const int dx = px - cx;
            const int dy = py - cy;
            if (dx * dx + dy * dy > r2) continue;
            const VoxelCoord c = plane_to_voxel(vol, axis, index, px, py);
            if (!mask.in_bounds(c.x, c.y, c.z)) continue;
            if (mask.at(c.x, c.y, c.z) != value) {
                mask.set(c.x, c.y, c.z, value);
                ++changed;
            }
        }
    }
    return changed;
}

} // namespace lumen
