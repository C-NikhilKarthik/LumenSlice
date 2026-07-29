#include "visualization/volume_texture.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace lumen {

void ExtractVolumeTexture(const Volume& volume, float level, float window,
                          int max_dimension, VolumeTexture& out) {
    out.clear();
    if (!volume.valid() || volume.width <= 0 || volume.height <= 0 ||
        volume.depth <= 0)
        return;

    const int largest = std::max({volume.width, volume.height, volume.depth});
    const int limit = max_dimension > 0 ? max_dimension : largest;
    const int step = std::max(1, (largest + limit - 1) / limit);
    out.width = (volume.width + step - 1) / step;
    out.height = (volume.height + step - 1) / step;
    out.depth = (volume.depth + step - 1) / step;
    out.voxels.resize(static_cast<std::size_t>(out.width) *
                      static_cast<std::size_t>(out.height) *
                      static_cast<std::size_t>(out.depth));

    const float safe_window = std::max(window, 1.0f);
    const float lower = level - safe_window * 0.5f;
    const float scale = 255.0f / safe_window;
    const auto encode = [&](float hu) -> std::uint8_t {
        const float normalized = (hu - lower) * scale;
        return static_cast<std::uint8_t>(std::clamp(normalized, 0.0f, 255.0f));
    };

    for (int z = 0; z < out.depth; ++z) {
        const int source_z = std::min(volume.depth - 1, z * step + step / 2);
        for (int y = 0; y < out.height; ++y) {
            const int source_y = std::min(volume.height - 1, y * step + step / 2);
            for (int x = 0; x < out.width; ++x) {
                const int source_x = std::min(volume.width - 1, x * step + step / 2);
                const std::size_t dst = static_cast<std::size_t>(x) +
                                        static_cast<std::size_t>(y) * out.width +
                                        static_cast<std::size_t>(z) * out.width *
                                            out.height;
                out.voxels[dst] = encode(volume.voxel_buffer[
                    volume.index(source_x, source_y, source_z)]);
            }
        }
    }
}

} // namespace lumen
