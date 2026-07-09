#include "segmentation/grow_from_seeds.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace lumen {

namespace {

// Inclusive bounding box of every non-zero (seed) voxel in the mask, expanded by
// `margin` and clamped to the volume. Returns false if there are no seeds.
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
    x0 = std::max(0, x0 - margin); y0 = std::max(0, y0 - margin);
    z0 = std::max(0, z0 - margin);
    x1 = std::min(w - 1, x1 + margin); y1 = std::min(h - 1, y1 + margin);
    z1 = std::min(d - 1, z1 + margin);
    return true;
}

} // namespace

long grow_from_seeds(const Volume& vol, LabelVolume& mask, int max_iters,
                     int margin) {
    if (!vol.valid() || !mask.valid()) return 0;
    if (mask.width() != vol.width || mask.height() != vol.height ||
        mask.depth() != vol.depth) {
        return 0;
    }
    if (max_iters < 1) max_iters = 1;
    if (margin < 0) margin = 0;

    int x0, y0, z0, x1, y1, z1;
    if (!seed_bbox(mask, margin, x0, y0, z0, x1, y1, z1)) return 0;

    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1, bd = z1 - z0 + 1;
    const std::size_t box = static_cast<std::size_t>(bw) *
                            static_cast<std::size_t>(bh) *
                            static_cast<std::size_t>(bd);

    // HU-similarity normaliser: closer HU ⇒ stronger attack. A global range is a
    // fine, cheap scale — the competition still routes boundaries to HU edges.
    const float denom = std::max(1e-3f, vol.hu_max - vol.hu_min);

    // Local box index (x,y,z are box-relative).
    auto bidx = [bw, bh](int x, int y, int z) -> std::size_t {
        return static_cast<std::size_t>(x) +
               static_cast<std::size_t>(y) * static_cast<std::size_t>(bw) +
               static_cast<std::size_t>(z) * static_cast<std::size_t>(bw) *
                   static_cast<std::size_t>(bh);
    };

    std::vector<std::uint8_t> cur_lbl(box), next_lbl(box);
    std::vector<float> cur_str(box, 0.0f), next_str(box, 0.0f);
    std::vector<float> hu(box); // cached HU per box voxel

    for (int z = 0; z < bd; ++z) {
        for (int y = 0; y < bh; ++y) {
            for (int x = 0; x < bw; ++x) {
                const std::size_t bi = bidx(x, y, z);
                const int vx = x0 + x, vy = y0 + y, vz = z0 + z;
                const std::uint8_t lbl = mask.at(vx, vy, vz);
                cur_lbl[bi] = lbl;
                cur_str[bi] = lbl != 0 ? 1.0f : 0.0f; // seeds start at full strength
                hu[bi] = vol.voxel_buffer[vol.index(vx, vy, vz)];
            }
        }
    }

    const int nx[6] = {-1, 1, 0, 0, 0, 0};
    const int ny[6] = {0, 0, -1, 1, 0, 0};
    const int nz[6] = {0, 0, 0, 0, -1, 1};

    for (int iter = 0; iter < max_iters; ++iter) {
        next_lbl = cur_lbl; // unattacked voxels keep their current owner
        next_str = cur_str;
        bool changed = false;
        for (int z = 0; z < bd; ++z) {
            for (int y = 0; y < bh; ++y) {
                for (int x = 0; x < bw; ++x) {
                    const std::size_t bi = bidx(x, y, z);
                    const float hu_a = hu[bi];
                    float best_str = cur_str[bi];
                    std::uint8_t best_lbl = cur_lbl[bi];
                    for (int k = 0; k < 6; ++k) {
                        const int ax = x + nx[k], ay = y + ny[k], az = z + nz[k];
                        if (ax < 0 || ay < 0 || az < 0 || ax >= bw || ay >= bh ||
                            az >= bd) {
                            continue;
                        }
                        const std::size_t ni = bidx(ax, ay, az);
                        if (cur_str[ni] <= 0.0f || cur_lbl[ni] == 0) continue;
                        const float g =
                            1.0f - std::fabs(hu_a - hu[ni]) / denom;
                        if (g <= 0.0f) continue;
                        const float attack = cur_str[ni] * g;
                        // Strict '>' means a seed at strength 1 can never be
                        // overwritten, so painted strokes are preserved.
                        if (attack > best_str) {
                            best_str = attack;
                            best_lbl = cur_lbl[ni];
                        }
                    }
                    if (best_lbl != cur_lbl[bi]) changed = true;
                    next_lbl[bi] = best_lbl;
                    next_str[bi] = best_str;
                }
            }
        }
        cur_lbl.swap(next_lbl);
        cur_str.swap(next_str);
        if (!changed) break;
    }

    // Write winners back into the live mask; count relabelled voxels.
    long changed_count = 0;
    for (int z = 0; z < bd; ++z) {
        for (int y = 0; y < bh; ++y) {
            for (int x = 0; x < bw; ++x) {
                const std::size_t bi = bidx(x, y, z);
                const int vx = x0 + x, vy = y0 + y, vz = z0 + z;
                const std::uint8_t final_lbl = cur_lbl[bi];
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
