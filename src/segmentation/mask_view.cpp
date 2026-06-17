#include "segmentation/mask_view.hpp"

#include <cstddef>
#include <cstdint>

#include "geometry/plane_map.hpp"

namespace lumen {
namespace {

// Overlay opacity (the CGImage on the Swift side is premultipliedLast): stored
// channel = colour * alpha / 255.
constexpr std::uint8_t kAlpha = 130;

inline std::uint8_t premul(std::uint8_t c) {
    return static_cast<std::uint8_t>(static_cast<int>(c) * kAlpha / 255);
}

} // namespace

void ExtractMaskSlice(const Volume& vol, const LabelVolume& mask,
                      const SegmentTable& table, Axis axis, int index,
                      SliceImage& out) {
    const SliceDims d = slice_dims(vol, axis);
    out.width = d.width;
    out.height = d.height;
    out.rgba.assign(static_cast<std::size_t>(d.width) * d.height * 4, 0);
    if (!vol.valid() || !mask.valid()) return;

    const Rgb* colors = table.color_table();
    const unsigned char* visible = table.visible_table();

    for (int py = 0; py < d.height; ++py) {
        for (int px = 0; px < d.width; ++px) {
            const VoxelCoord c = plane_to_voxel(vol, axis, index, px, py);
            const std::uint8_t id = mask.at(c.x, c.y, c.z);
            if (id == 0 || visible[id] == 0) continue;
            const Rgb col = colors[id];
            std::uint8_t* p =
                &out.rgba[(static_cast<std::size_t>(py) * d.width + px) * 4];
            p[0] = premul(col.r);
            p[1] = premul(col.g);
            p[2] = premul(col.b);
            p[3] = kAlpha;
        }
    }
}

} // namespace lumen
