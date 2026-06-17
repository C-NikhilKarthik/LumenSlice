#include "segmentation/mask_view.hpp"

#include <cstddef>
#include <cstdint>

#include "geometry/plane_map.hpp"

namespace lumen {
namespace {

// Active overlay colour, premultiplied alpha (the CGImage on the Swift side is
// premultipliedLast). Cyan at ~51% alpha: stored = colour * alpha / 255.
constexpr std::uint8_t kAlpha = 130;
constexpr std::uint8_t kR = 0;   //   0 * 130 / 255
constexpr std::uint8_t kG = 102; // 200 * 130 / 255
constexpr std::uint8_t kB = 130; // 255 * 130 / 255

} // namespace

void ExtractMaskSlice(const Volume& vol, const LabelVolume& mask, Axis axis,
                      int index, SliceImage& out) {
    const SliceDims d = slice_dims(vol, axis);
    out.width = d.width;
    out.height = d.height;
    out.rgba.assign(static_cast<std::size_t>(d.width) * d.height * 4, 0);
    if (!vol.valid() || !mask.valid()) return;

    for (int py = 0; py < d.height; ++py) {
        for (int px = 0; px < d.width; ++px) {
            const VoxelCoord c = plane_to_voxel(vol, axis, index, px, py);
            if (mask.at(c.x, c.y, c.z) == 0) continue;
            std::uint8_t* p =
                &out.rgba[(static_cast<std::size_t>(py) * d.width + px) * 4];
            p[0] = kR;
            p[1] = kG;
            p[2] = kB;
            p[3] = kAlpha;
        }
    }
}

} // namespace lumen
