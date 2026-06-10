#include "visualization/slice_view.h"

#include <algorithm>

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

    // Output dimensions depend on which plane we're slicing.
    switch (axis) {
    case Axis::Axial: // XY plane at fixed Z.
        out.width = vol.width;
        out.height = vol.height;
        break;
    case Axis::Coronal: // XZ plane at fixed Y.
        out.width = vol.width;
        out.height = vol.depth;
        break;
    case Axis::Sagittal: // YZ plane at fixed X.
        out.width = vol.height;
        out.height = vol.depth;
        break;
    }
    out.rgba.assign(static_cast<size_t>(out.width) * out.height * 4, 0);

    const float* v = vol.voxel_buffer.get();

    switch (axis) {
    case Axis::Axial: {
        const int z = index;
        for (int y = 0; y < vol.height; ++y)
            for (int x = 0; x < vol.width; ++x)
                PutGray(out, x, y, Apply(v[vol.index(x, y, z)], lower, inv_span));
        break;
    }
    case Axis::Coronal: {
        const int y = index;
        // Flip vertically so increasing Z (superior) renders toward the top.
        for (int z = 0; z < vol.depth; ++z)
            for (int x = 0; x < vol.width; ++x)
                PutGray(out, x, vol.depth - 1 - z,
                        Apply(v[vol.index(x, y, z)], lower, inv_span));
        break;
    }
    case Axis::Sagittal: {
        const int x = index;
        for (int z = 0; z < vol.depth; ++z)
            for (int y = 0; y < vol.height; ++y)
                PutGray(out, y, vol.depth - 1 - z,
                        Apply(v[vol.index(x, y, z)], lower, inv_span));
        break;
    }
    }
}

} // namespace lumen
