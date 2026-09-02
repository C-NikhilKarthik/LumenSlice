#include "segmentation/grow_from_seeds.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <queue>
#include <vector>

namespace lumen {

namespace {

// Inclusive bounding box of every non-zero (seed) voxel in the mask, expanded by
// `margin` and clamped to the volume. A negative margin follows Slicer's
// extentGrowthRatio rule independently on each axis. Returns false if there are
// no seeds.
bool seed_bbox(const LabelVolume& mask, int margin, int& x0, int& y0, int& z0,
               int& x1, int& y1, int& z1) {
    const int w = mask.width(), h = mask.height(), d = mask.depth();
    const std::uint8_t* data = mask.data();
    x0 = w; y0 = h; z0 = d;
    x1 = -1; y1 = -1; z1 = -1;
    std::size_t i = 0;
    for (int z = 0; z < d; ++z) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x, ++i) {
                if (data[i] == 0) continue;
                x0 = std::min(x0, x); y0 = std::min(y0, y); z0 = std::min(z0, z);
                x1 = std::max(x1, x); y1 = std::max(y1, y); z1 = std::max(z1, z);
            }
        }
    }
    if (x1 < 0) return false; // no seeds
    const int mx = margin < 0 ? std::max(3, int(std::ceil(0.5 * (x1 - x0))) ) : margin;
    const int my = margin < 0 ? std::max(3, int(std::ceil(0.5 * (y1 - y0))) ) : margin;
    const int mz = margin < 0 ? std::max(3, int(std::ceil(0.5 * (z1 - z0))) ) : margin;
    x0 = std::max(0, x0 - mx); y0 = std::max(0, y0 - my);
    z0 = std::max(0, z0 - mz);
    x1 = std::min(w - 1, x1 + mx); y1 = std::min(h - 1, y1 + my);
    z1 = std::min(d - 1, z1 + mz);
    return true;
}

} // namespace

long grow_from_seeds(const Volume& vol, LabelVolume& mask, int max_iters,
                     int margin, float distance_penalty, bool restrict_to_hu,
                     float hu_lo, float hu_hi) {
    if (!vol.valid() || !mask.valid()) return 0;
    if (mask.width() != vol.width || mask.height() != vol.height ||
        mask.depth() != vol.depth) {
        return 0;
    }
    if (max_iters < 1) max_iters = 1;
    distance_penalty = std::max(0.0f, distance_penalty);
    if (hu_lo > hu_hi) std::swap(hu_lo, hu_hi);

    int x0, y0, z0, x1, y1, z1;
    if (!seed_bbox(mask, margin, x0, y0, z0, x1, y1, z1)) return 0;
    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1, bd = z1 - z0 + 1;
    const std::size_t box = static_cast<std::size_t>(bw) *
                            static_cast<std::size_t>(bh) *
                            static_cast<std::size_t>(bd);

    // Local box index (x,y,z are box-relative).
    auto bidx = [bw, bh](int x, int y, int z) -> std::size_t {
        return static_cast<std::size_t>(x) +
               static_cast<std::size_t>(y) * static_cast<std::size_t>(bw) +
               static_cast<std::size_t>(z) * static_cast<std::size_t>(bw) *
                   static_cast<std::size_t>(bh);
    };

    std::vector<std::uint8_t> labels(box, 0);
    std::vector<float> distance(box, std::numeric_limits<float>::max());
    std::vector<float> hu(box); // cached HU per box voxel

    for (int z = 0; z < bd; ++z) {
        for (int y = 0; y < bh; ++y) {
            for (int x = 0; x < bw; ++x) {
                const std::size_t bi = bidx(x, y, z);
                const int vx = x0 + x, vy = y0 + y, vz = z0 + z;
                const std::uint8_t lbl = mask.at(vx, vy, vz);
                labels[bi] = lbl;
                hu[bi] = vol.voxel_buffer[vol.index(vx, vy, vz)];
            }
        }
    }

    // This is Slicer's current FastGrowCut formulation: a Dijkstra shortest-path
    // propagation over a 26-neighbor physical neighborhood. The path cost is the
    // accumulated absolute HU difference plus the distance penalty per millimetre.
    // It converges by exhausting the heap; max_iters remains only for API
    // compatibility with the previous iterative kernel.
    (void)max_iters;
    struct Candidate {
        float cost;
        std::size_t index;
        bool operator>(const Candidate& other) const {
            return cost != other.cost ? cost > other.cost : index > other.index;
        }
    };
    struct Neighbor {
        int dx, dy, dz;
        float distance;
    };
    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> heap;
    std::vector<Neighbor> neighbors;
    neighbors.reserve(26);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                const float physicalDistance = std::sqrt(
                    (dx * vol.spacing_x) * (dx * vol.spacing_x) +
                    (dy * vol.spacing_y) * (dy * vol.spacing_y) +
                    (dz * vol.spacing_z) * (dz * vol.spacing_z));
                neighbors.push_back({dx, dy, dz, physicalDistance});
            }
        }
    }
    constexpr float epsilon = 1e-3f;
    for (std::size_t bi = 0; bi < box; ++bi) {
        if (labels[bi] == 0) continue;
        distance[bi] = epsilon;
        heap.push({epsilon, bi});
    }
    if (heap.empty()) return 0;

    while (!heap.empty()) {
        const Candidate current = heap.top();
        heap.pop();
        if (current.cost > distance[current.index] + epsilon) continue;
        const int cz = int(current.index / (std::size_t(bw) * bh));
        const std::size_t rem = current.index % (std::size_t(bw) * bh);
        const int cy = int(rem / bw);
        const int cx = int(rem % bw);
        for (const Neighbor& n : neighbors) {
            const int nx = cx + n.dx, ny = cy + n.dy, nz = cz + n.dz;
            if (nx < 0 || ny < 0 || nz < 0 || nx >= bw || ny >= bh || nz >= bd)
                continue;
            const std::size_t ni = bidx(nx, ny, nz);
            // Existing seeds remain valid sources. Only newly claimed voxels
            // are constrained by the active intensity mask.
            if (restrict_to_hu && labels[ni] == 0 &&
                (hu[ni] < hu_lo || hu[ni] > hu_hi)) {
                continue;
            }
            const float newCost = current.cost +
                std::fabs(hu[current.index] - hu[ni]) +
                distance_penalty * n.distance;
            if (newCost < distance[ni]) {
                distance[ni] = newCost;
                labels[ni] = labels[current.index];
                heap.push({newCost, ni});
            }
        }
    }

    // Write winners back into the live mask; count relabelled voxels.
    long changed_count = 0;
    for (int z = 0; z < bd; ++z) {
        for (int y = 0; y < bh; ++y) {
            for (int x = 0; x < bw; ++x) {
                const std::size_t bi = bidx(x, y, z);
                const int vx = x0 + x, vy = y0 + y, vz = z0 + z;
                const std::uint8_t final_lbl = labels[bi];
                if (final_lbl != mask.at(vx, vy, vz)) {
                    mask.set(vx, vy, vz, final_lbl);
                    ++changed_count;
                }
            }
        }
    }
    return changed_count;
}

} // namespace lumen
