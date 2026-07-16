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

void threshold_fill(const Volume& vol, float lo, float hi, LabelVolume& mask,
                    std::uint8_t label) {
    if (!vol.valid() || !mask.valid() || label == 0) return;
    if (lo > hi) std::swap(lo, hi);

    const float* hu = vol.voxel_buffer.get();
    std::uint8_t* out = mask.data();
    const std::size_t n = mask.voxel_count();
    for (std::size_t i = 0; i < n; ++i) {
        const bool in = hu[i] >= lo && hu[i] <= hi;
        if (out[i] == label) {
            out[i] = in ? label : 0; // keep own voxel only while still in range
        } else if (out[i] == 0 && in) {
            out[i] = label;          // claim background in range
        }
        // voxels owned by other segments are left untouched
    }
}

long region_grow(const Volume& vol, int sx, int sy, int sz, float tol,
                 LabelVolume& mask, std::uint8_t label) {
    if (!vol.valid() || !mask.valid() || label == 0) return 0;
    if (!mask.in_bounds(sx, sy, sz)) return 0;
    if (mask.at(sx, sy, sz) != 0) return 0; // seed already labelled

    const float seed_hu = vol.voxel_buffer[vol.index(sx, sy, sz)];
    const float lo = seed_hu - tol;
    const float hi = seed_hu + tol;

    // The mask doubles as the visited set: a voxel is enqueued exactly once, at
    // which point it is labelled, so it can never be revisited. That bounds the
    // whole fill at voxel_count().
    std::vector<VoxelCoord> stack;
    stack.push_back({sx, sy, sz});
    mask.set(sx, sy, sz, label);
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
            mask.set(nx, ny, nz, label);
            stack.push_back({nx, ny, nz});
            ++added;
        }
    }
    return added;
}

long paint_disk(const Volume& vol, Axis axis, int index, int cx, int cy,
                int radius, bool add, LabelVolume& mask, std::uint8_t label) {
    if (!vol.valid() || !mask.valid() || radius < 0 || label == 0) return 0;

    const SliceDims dims = slice_dims(vol, axis);
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
            const std::uint8_t cur = mask.at(c.x, c.y, c.z);
            if (add) {
                if (cur != label) { // brush wins: overwrite whatever is here
                    mask.set(c.x, c.y, c.z, label);
                    ++changed;
                }
            } else if (cur == label) { // erase only the active segment
                mask.set(c.x, c.y, c.z, 0);
                ++changed;
            }
        }
    }
    return changed;
}

long level_trace(const Volume& vol, Axis axis, int index, int cx, int cy,
                 LabelVolume& mask, std::uint8_t label) {
    if (!vol.valid() || !mask.valid() || label == 0) return 0;

    const SliceDims dims = slice_dims(vol, axis);
    if (cx < 0 || cy < 0 || cx >= dims.width || cy >= dims.height) return 0;

    const VoxelCoord seed = plane_to_voxel(vol, axis, index, cx, cy);
    if (!mask.in_bounds(seed.x, seed.y, seed.z)) return 0;
    const std::uint8_t seed_owner = mask.at(seed.x, seed.y, seed.z);
    if (seed_owner != 0 && seed_owner != label) return 0; // owned by another segment
    const float level = vol.voxel_buffer[vol.index(seed.x, seed.y, seed.z)];

    // A 2D visited grid over the slice bounds the flood at slice area; each pixel
    // maps through plane_map so the coronal/sagittal flip is honoured.
    std::vector<std::uint8_t> visited(
        static_cast<std::size_t>(dims.width) * static_cast<std::size_t>(dims.height),
        0);
    const auto vidx = [&](int px, int py) {
        return static_cast<std::size_t>(py) * static_cast<std::size_t>(dims.width) +
               static_cast<std::size_t>(px);
    };

    std::vector<std::array<int, 2>> stack;
    long changed = 0;
    const auto try_push = [&](int px, int py) {
        if (px < 0 || py < 0 || px >= dims.width || py >= dims.height) return;
        if (visited[vidx(px, py)]) return;
        visited[vidx(px, py)] = 1;
        const VoxelCoord c = plane_to_voxel(vol, axis, index, px, py);
        if (!mask.in_bounds(c.x, c.y, c.z)) return;
        if (vol.voxel_buffer[vol.index(c.x, c.y, c.z)] < level) return; // below level
        const std::uint8_t cur = mask.at(c.x, c.y, c.z);
        if (cur != 0 && cur != label) return; // barrier: another segment's voxel
        stack.push_back({px, py});
    };

    try_push(cx, cy);
    constexpr std::array<std::array<int, 2>, 4> d4 = {{
        {{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}},
    }};
    while (!stack.empty()) {
        const std::array<int, 2> p = stack.back();
        stack.pop_back();
        const VoxelCoord c = plane_to_voxel(vol, axis, index, p[0], p[1]);
        if (mask.at(c.x, c.y, c.z) != label) {
            mask.set(c.x, c.y, c.z, label);
            ++changed;
        }
        for (const auto& d : d4) try_push(p[0] + d[0], p[1] + d[1]);
    }
    return changed;
}

} // namespace lumen
