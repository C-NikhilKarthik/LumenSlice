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

} // namespace lumen
