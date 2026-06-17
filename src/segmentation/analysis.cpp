#include "segmentation/analysis.hpp"

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

} // namespace lumen
