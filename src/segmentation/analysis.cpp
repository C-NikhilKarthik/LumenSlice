#include "segmentation/analysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace lumen {
namespace {

// 6-connected neighbour offsets (shared shape with region_grow).
constexpr int kNx[6] = {1, -1, 0, 0, 0, 0};
constexpr int kNy[6] = {0, 0, 1, -1, 0, 0};
constexpr int kNz[6] = {0, 0, 0, 0, 1, -1};

// Label every 6-connected component of `label` voxels. Fills `comp` (one int per
// voxel: 0 = not this label, else 1-based component id) and `sizes` (voxel count
// per component, index 0 unused). Returns the number of components.
int label_components(const LabelVolume& mask, std::uint8_t label,
                     std::vector<int>& comp, std::vector<long>& sizes) {
    const int w = mask.width(), h = mask.height(), d = mask.depth();
    comp.assign(mask.voxel_count(), 0);
    sizes.assign(1, 0); // sizes[0] unused
    const std::uint8_t* data = mask.data();

    int next = 0;
    std::vector<std::size_t> stack;
    for (std::size_t start = 0; start < mask.voxel_count(); ++start) {
        if (data[start] != label || comp[start] != 0) continue;
        ++next;
        sizes.push_back(0);
        comp[start] = next;
        stack.push_back(start);
        while (!stack.empty()) {
            const std::size_t idx = stack.back();
            stack.pop_back();
            ++sizes[static_cast<std::size_t>(next)];
            const int z = static_cast<int>(idx / (static_cast<std::size_t>(w) * h));
            const int rem = static_cast<int>(idx % (static_cast<std::size_t>(w) * h));
            const int y = rem / w;
            const int x = rem % w;
            for (int n = 0; n < 6; ++n) {
                const int nx = x + kNx[n], ny = y + kNy[n], nz = z + kNz[n];
                if (nx < 0 || ny < 0 || nz < 0 || nx >= w || ny >= h || nz >= d)
                    continue;
                const std::size_t nidx = mask.index(nx, ny, nz);
                if (data[nidx] != label || comp[nidx] != 0) continue;
                comp[nidx] = next;
                stack.push_back(nidx);
            }
        }
    }
    return next;
}

} // namespace

float otsu_threshold(const Volume& vol) {
    if (!vol.valid() || vol.hu_max <= vol.hu_min) return vol.hu_min;

    constexpr int kBins = 256;
    const float lo = vol.hu_min;
    const float hi = vol.hu_max;
    const float scale = static_cast<float>(kBins) / (hi - lo);

    std::array<long, kBins> hist{};
    const float* hu = vol.voxel_buffer.get();
    const std::size_t n = vol.voxel_count();
    for (std::size_t i = 0; i < n; ++i) {
        int bin = static_cast<int>((hu[i] - lo) * scale);
        if (bin < 0) bin = 0;
        if (bin >= kBins) bin = kBins - 1;
        ++hist[static_cast<std::size_t>(bin)];
    }

    double total = static_cast<double>(n);
    double sum = 0;
    for (int i = 0; i < kBins; ++i) sum += static_cast<double>(i) * hist[static_cast<std::size_t>(i)];

    double sum_b = 0, w_b = 0, max_var = -1;
    int best = 0;
    for (int i = 0; i < kBins; ++i) {
        w_b += static_cast<double>(hist[static_cast<std::size_t>(i)]);
        if (w_b == 0) continue;
        const double w_f = total - w_b;
        if (w_f == 0) break;
        sum_b += static_cast<double>(i) * hist[static_cast<std::size_t>(i)];
        const double m_b = sum_b / w_b;
        const double m_f = (sum - sum_b) / w_f;
        const double var = w_b * w_f * (m_b - m_f) * (m_b - m_f);
        if (var > max_var) {
            max_var = var;
            best = i;
        }
    }
    // Map the winning bin's upper edge back to an HU value.
    return lo + (static_cast<float>(best) + 1.0f) / scale;
}

long keep_largest_island(LabelVolume& mask, std::uint8_t label) {
    if (!mask.valid() || label == 0) return 0;
    std::vector<int> comp;
    std::vector<long> sizes;
    const int ncomp = label_components(mask, label, comp, sizes);
    if (ncomp <= 1) return 0;

    int best = 1;
    for (int c = 2; c <= ncomp; ++c) {
        if (sizes[static_cast<std::size_t>(c)] > sizes[static_cast<std::size_t>(best)])
            best = c;
    }
    std::uint8_t* data = mask.data();
    long removed = 0;
    for (std::size_t i = 0; i < mask.voxel_count(); ++i) {
        if (comp[i] != 0 && comp[i] != best) {
            data[i] = 0;
            ++removed;
        }
    }
    return removed;
}

long remove_small_islands(LabelVolume& mask, std::uint8_t label, long min_voxels) {
    if (!mask.valid() || label == 0 || min_voxels <= 1) return 0;
    std::vector<int> comp;
    std::vector<long> sizes;
    const int ncomp = label_components(mask, label, comp, sizes);
    if (ncomp == 0) return 0;

    std::uint8_t* data = mask.data();
    long removed = 0;
    for (std::size_t i = 0; i < mask.voxel_count(); ++i) {
        const int c = comp[i];
        if (c != 0 && sizes[static_cast<std::size_t>(c)] < min_voxels) {
            data[i] = 0;
            ++removed;
        }
    }
    return removed;
}

long dilate_label(LabelVolume& mask, std::uint8_t label, int iterations) {
    if (!mask.valid() || label == 0 || iterations <= 0) return 0;
    const int w = mask.width(), h = mask.height(), d = mask.depth();
    std::uint8_t* data = mask.data();
    long added = 0;
    std::vector<std::size_t> frontier;
    for (int it = 0; it < iterations; ++it) {
        frontier.clear();
        for (int z = 0; z < d; ++z)
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const std::size_t idx = mask.index(x, y, z);
                    if (data[idx] != 0) continue; // only claim background
                    bool touches = false;
                    for (int n = 0; n < 6 && !touches; ++n) {
                        const int nx = x + kNx[n], ny = y + kNy[n], nz = z + kNz[n];
                        if (nx < 0 || ny < 0 || nz < 0 || nx >= w || ny >= h || nz >= d)
                            continue;
                        if (data[mask.index(nx, ny, nz)] == label) touches = true;
                    }
                    if (touches) frontier.push_back(idx);
                }
        for (std::size_t idx : frontier) { data[idx] = label; ++added; }
        if (frontier.empty()) break;
    }
    return added;
}

long erode_label(LabelVolume& mask, std::uint8_t label, int iterations) {
    if (!mask.valid() || label == 0 || iterations <= 0) return 0;
    const int w = mask.width(), h = mask.height(), d = mask.depth();
    std::uint8_t* data = mask.data();
    long removed = 0;
    std::vector<std::size_t> boundary;
    for (int it = 0; it < iterations; ++it) {
        boundary.clear();
        for (int z = 0; z < d; ++z)
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const std::size_t idx = mask.index(x, y, z);
                    if (data[idx] != label) continue;
                    bool edge = false;
                    for (int n = 0; n < 6 && !edge; ++n) {
                        const int nx = x + kNx[n], ny = y + kNy[n], nz = z + kNz[n];
                        if (nx < 0 || ny < 0 || nz < 0 || nx >= w || ny >= h ||
                            nz >= d) {
                            edge = true; // volume face counts as outside
                        } else if (data[mask.index(nx, ny, nz)] != label) {
                            edge = true;
                        }
                    }
                    if (edge) boundary.push_back(idx);
                }
        for (std::size_t idx : boundary) { data[idx] = 0; ++removed; }
        if (boundary.empty()) break;
    }
    return removed;
}

long smooth_label(LabelVolume& mask, std::uint8_t label, int iterations) {
    if (!mask.valid() || label == 0 || iterations <= 0) return 0;
    const int w = mask.width(), h = mask.height(), d = mask.depth();
    std::uint8_t* data = mask.data();
    const std::size_t n = mask.voxel_count();
    std::vector<std::uint8_t> src(n);
    long changed = 0;
    for (int it = 0; it < iterations; ++it) {
        for (std::size_t i = 0; i < n; ++i) src[i] = (data[i] == label) ? 1 : 0;
        for (int z = 0; z < d; ++z)
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    int count = 0, total = 0;
                    for (int dz = -1; dz <= 1; ++dz)
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx) {
                                const int nx = x + dx, ny = y + dy, nz = z + dz;
                                if (nx < 0 || ny < 0 || nz < 0 || nx >= w ||
                                    ny >= h || nz >= d)
                                    continue;
                                ++total;
                                count += src[mask.index(nx, ny, nz)];
                            }
                    const bool want = count * 2 > total; // strict majority -> label
                    const std::size_t idx = mask.index(x, y, z);
                    if (want && data[idx] == 0) {
                        data[idx] = label; // claim background only
                        ++changed;
                    } else if (!want && data[idx] == label) {
                        data[idx] = 0;     // clear own label only
                        ++changed;
                    }
                }
    }
    return changed;
}

long grow_from_seeds(const Volume& vol, LabelVolume& mask, float tolerance) {
    if (!vol.valid() || !mask.valid()) return 0;
    if (vol.voxel_count() != mask.voxel_count()) return 0;

    const int w = mask.width();
    const int h = mask.height();
    const int d = mask.depth();
    const std::size_t n = mask.voxel_count();
    std::uint8_t* out = mask.data();
    const float* hu = vol.voxel_buffer.get();

    // Bottleneck (minimax) path cost is an integer HU step in [0, kMaxCost]. Since
    // max(current_cost, edge) is never below the current cost, processing a bucket
    // queue in increasing cost visits every voxel exactly once - Dial's algorithm,
    // an O(voxels) Dijkstra. dist holds each voxel's best cost so far; a voxel is
    // finalized (its label fixed) when first popped. `maxCost` caps the grow at the
    // tolerance so it stops at intensity edges (tolerance <= 0 -> fill everything).
    constexpr int kMaxCost = 4095;
    constexpr std::uint16_t kInf = 0xFFFF;
    const int maxCost = (tolerance <= 0.0f)
                            ? kMaxCost
                            : std::min(kMaxCost,
                                       static_cast<int>(std::lround(tolerance)));
    std::vector<std::uint16_t> dist(n, kInf);
    std::vector<std::uint8_t> done(n, 0);
    std::vector<std::vector<std::size_t>> buckets(
        static_cast<std::size_t>(maxCost) + 1);

    long seeds = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (out[i] != 0) { dist[i] = 0; buckets[0].push_back(i); ++seeds; }
    }
    if (seeds == 0) return 0;

    for (int c = 0; c <= maxCost; ++c) {
        std::vector<std::size_t>& bucket = buckets[c];
        // A cost-0 (equal-HU) relaxation appends to the current bucket, so index it
        // by hand rather than a range-for, which would miss the appended entries.
        for (std::size_t bi = 0; bi < bucket.size(); ++bi) {
            const std::size_t i = bucket[bi];
            if (done[i] || dist[i] != static_cast<std::uint16_t>(c)) continue;
            done[i] = 1;
            const std::uint8_t lbl = out[i];
            const int x = static_cast<int>(i % static_cast<std::size_t>(w));
            const int y = static_cast<int>((i / static_cast<std::size_t>(w)) %
                                           static_cast<std::size_t>(h));
            const int z = static_cast<int>(
                i / (static_cast<std::size_t>(w) * static_cast<std::size_t>(h)));
            for (int k = 0; k < 6; ++k) {
                const int xn = x + kNx[k], yn = y + kNy[k], zn = z + kNz[k];
                if (xn < 0 || yn < 0 || zn < 0 || xn >= w || yn >= h || zn >= d)
                    continue;
                const std::size_t j = mask.index(xn, yn, zn);
                if (done[j]) continue;
                int e = static_cast<int>(std::lround(std::fabs(hu[j] - hu[i])));
                if (e > kMaxCost) e = kMaxCost;
                const int nc = c > e ? c : e; // minimax: worst step on the path
                if (nc > maxCost) continue;   // beyond tolerance: leave background
                if (nc < dist[j]) {
                    dist[j] = static_cast<std::uint16_t>(nc);
                    out[j] = lbl; // tentative; fixed when j is popped
                    buckets[nc].push_back(j);
                }
            }
        }
        std::vector<std::size_t>().swap(bucket); // free this cost level's memory
    }

    return mask.count_nonzero() - seeds;
}

} // namespace lumen
