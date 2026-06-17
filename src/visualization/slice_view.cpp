#include "visualization/slice_view.h"

#include <algorithm>

#include "geometry/plane_map.hpp"

namespace lumen {
namespace {

inline uint8_t Apply(float hu, float lower, float inv_span) {
    float t = (hu - lower) * inv_span;
    t = std::clamp(t, 0.0f, 1.0f);
    return static_cast<uint8_t>(t * 255.0f + 0.5f);
}

inline void PutGray(SliceImage& img, int x, int y, uint8_t g) {
    uint8_t* p = &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
    p[0] = g;
    p[1] = g;
    p[2] = g;
    p[3] = 255;
}

} // namespace

void ExtractSlice(const Volume& vol, Axis axis, int index, float level,
                  float window, SliceImage& out) {
    if (!vol.valid()) {
        out.width = out.height = 0;
        out.rgba.clear();
        return;
    }

    const int slices = vol.slice_count(axis);
    index = std::clamp(index, 0, slices - 1);

    if (window < 1.0f) window = 1.0f; // avoid divide-by-zero on a flat window.
    const float lower = level - window * 0.5f;
    const float inv_span = 1.0f / window;

    // Output dimensions + the pixel->voxel mapping (incl. the coronal/sagittal
    // vertical flip) come from plane_map — the single source of truth shared with
    // the mask overlay and segmentation picking. See src/geometry/plane_map.hpp.
    const SliceDims d = slice_dims(vol, axis);
    out.width = d.width;
    out.height = d.height;
    out.rgba.assign(static_cast<size_t>(out.width) * out.height * 4, 0);

    const float* v = vol.voxel_buffer.get();
    for (int py = 0; py < out.height; ++py) {
        for (int px = 0; px < out.width; ++px) {
            const VoxelCoord c = plane_to_voxel(vol, axis, index, px, py);
            PutGray(out, px, py,
                    Apply(v[vol.index(c.x, c.y, c.z)], lower, inv_span));
        }
    }
}

} // namespace lumen
