// LumenSlice - per-segment quantitative statistics (see statistics.hpp).

#include "segmentation/statistics.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace lumen {

SegmentStats compute_label_stats(const LabelVolume& mask, const Volume& vol,
                                 std::uint8_t label) {
    SegmentStats s;
    if (label == 0 || !mask.valid() || !vol.valid()) return s;

    const int W = mask.width(), H = mask.height(), D = mask.depth();
    // The mask is sized to the volume on load; guard against a stale mismatch so we
    // never index the HU buffer out of range.
    if (W != vol.width || H != vol.height || D != vol.depth) return s;

    const double sx = vol.spacing_x, sy = vol.spacing_y, sz = vol.spacing_z;
    // Physical area of a voxel face, per the axis whose neighbour it faces.
    const double face_x = sy * sz;  // face crossed by a step in x
    const double face_y = sx * sz;  // ... in y
    const double face_z = sx * sy;  // ... in z

    const std::uint8_t* m = mask.data();
    const float* hu = vol.voxel_buffer.get();

    long count = 0;
    double area = 0.0;
    double sum = 0.0, sumsq = 0.0;
    double lo = 0.0, hi = 0.0;
    bool first = true;

    for (int z = 0; z < D; ++z) {
        for (int y = 0; y < H; ++y) {
            const std::size_t row = (static_cast<std::size_t>(z) * H + y) * W;
            for (int x = 0; x < W; ++x) {
                if (m[row + x] != label) continue;
                ++count;

                const double v = hu[row + x];
                sum += v;
                sumsq += v * v;
                if (first) { lo = hi = v; first = false; }
                else { lo = std::min(lo, v); hi = std::max(hi, v); }

                // Add each of the 6 faces whose neighbour is not this same label.
                // mask.at() returns 0 (background) out of bounds, so the volume's
                // outer boundary correctly counts as exposed.
                if (mask.at(x - 1, y, z) != label) area += face_x;
                if (mask.at(x + 1, y, z) != label) area += face_x;
                if (mask.at(x, y - 1, z) != label) area += face_y;
                if (mask.at(x, y + 1, z) != label) area += face_y;
                if (mask.at(x, y, z - 1) != label) area += face_z;
                if (mask.at(x, y, z + 1) != label) area += face_z;
            }
        }
    }

    if (count == 0) return s;
    s.voxel_count = count;
    s.volume_mm3 = static_cast<double>(count) * sx * sy * sz;
    s.surface_area_mm2 = area;
    s.hu_min = lo;
    s.hu_max = hi;
    const double mean = sum / static_cast<double>(count);
    s.hu_mean = mean;
    // Population variance; clamp tiny negatives from floating-point cancellation.
    const double var = std::max(0.0, sumsq / static_cast<double>(count) - mean * mean);
    s.hu_stddev = std::sqrt(var);
    return s;
}

MeshMetrics compute_mesh_metrics(const Mesh& mesh) {
    MeshMetrics r;
    const auto& vtx = mesh.vertices;
    const auto& idx = mesh.indices;
    double area2 = 0.0;   // accumulates 2 * area, halved once at the end
    double vol6 = 0.0;    // accumulates 6 * signed volume, divided once at the end

    for (std::size_t i = 0; i + 2 < idx.size(); i += 3) {
        const std::size_t a = static_cast<std::size_t>(idx[i]) * 3;
        const std::size_t b = static_cast<std::size_t>(idx[i + 1]) * 3;
        const std::size_t c = static_cast<std::size_t>(idx[i + 2]) * 3;
        if (a + 2 >= vtx.size() || b + 2 >= vtx.size() || c + 2 >= vtx.size())
            continue;

        const double ax = vtx[a], ay = vtx[a + 1], az = vtx[a + 2];
        const double bx = vtx[b], by = vtx[b + 1], bz = vtx[b + 2];
        const double cx = vtx[c], cy = vtx[c + 1], cz = vtx[c + 2];

        // Surface area: |(b-a) x (c-a)| / 2.
        const double ux = bx - ax, uy = by - ay, uz = bz - az;
        const double vx = cx - ax, vy = cy - ay, vz = cz - az;
        const double crx = uy * vz - uz * vy;
        const double cry = uz * vx - ux * vz;
        const double crz = ux * vy - uy * vx;
        area2 += std::sqrt(crx * crx + cry * cry + crz * crz);

        // Signed volume of the tetrahedron (origin, a, b, c): a . (b x c).
        vol6 += ax * (by * cz - bz * cy) - ay * (bx * cz - bz * cx) +
                az * (bx * cy - by * cx);
    }

    r.surface_area_mm2 = 0.5 * area2;
    r.volume_mm3 = std::abs(vol6) / 6.0;
    return r;
}

} // namespace lumen
