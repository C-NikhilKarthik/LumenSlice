#include "segmentation/scissor.hpp"

#include <cstddef>

namespace lumen {

namespace {

// Even-odd ray cast in screen space. `poly_xy` holds n (x,y) pairs.
bool point_in_polygon(float px, float py, const float* poly_xy, int n) {
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const float xi = poly_xy[2 * i], yi = poly_xy[2 * i + 1];
        const float xj = poly_xy[2 * j], yj = poly_xy[2 * j + 1];
        const bool crosses = (yi > py) != (yj > py);
        if (crosses) {
            const float t = (py - yi) / (yj - yi);
            if (px < xi + t * (xj - xi)) inside = !inside;
        }
    }
    return inside;
}

} // namespace

long scissor_cut(const Volume& vol, LabelVolume& mask, const float* mvp, int vp_w,
                 int vp_h, const float* poly_xy, int poly_count, bool erase_inside,
                 std::uint8_t only_label) {
    if (!vol.valid() || !mask.valid() || mvp == nullptr || poly_xy == nullptr) {
        return 0;
    }
    if (poly_count < 3 || vp_w <= 0 || vp_h <= 0) return 0;

    const int w = mask.width(), h = mask.height(), d = mask.depth();
    const float sx = vol.spacing_x, sy = vol.spacing_y, sz = vol.spacing_z;
    std::uint8_t* data = mask.data();

    long cleared = 0;
    std::size_t i = 0;
    for (int z = 0; z < d; ++z) {
        const float wz = static_cast<float>(z) * sz;
        for (int y = 0; y < h; ++y) {
            const float wy = static_cast<float>(y) * sy;
            for (int x = 0; x < w; ++x, ++i) {
                const std::uint8_t lbl = data[i];
                if (lbl == 0) continue;
                if (only_label != 0 && lbl != only_label) continue;

                const float wx = static_cast<float>(x) * sx;
                // Row-vector transform: clip_j = Σ_i world_i · mvp[i*4+j].
                const float cx = wx * mvp[0] + wy * mvp[4] + wz * mvp[8] + mvp[12];
                const float cy = wx * mvp[1] + wy * mvp[5] + wz * mvp[9] + mvp[13];
                const float cw = wx * mvp[3] + wy * mvp[7] + wz * mvp[11] + mvp[15];
                if (cw <= 0.0f) continue; // behind the camera

                const float ndc_x = cx / cw, ndc_y = cy / cw;
                const float scr_x = (ndc_x * 0.5f + 0.5f) * static_cast<float>(vp_w);
                const float scr_y =
                    (1.0f - (ndc_y * 0.5f + 0.5f)) * static_cast<float>(vp_h);

                const bool inside =
                    point_in_polygon(scr_x, scr_y, poly_xy, poly_count);
                if (inside == erase_inside) {
                    data[i] = 0;
                    ++cleared;
                }
            }
        }
    }
    return cleared;
}

} // namespace lumen
