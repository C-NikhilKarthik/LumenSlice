// Shared scalar-volume preparation for GPU volume renderers.
#pragma once

#include <cstdint>
#include <vector>

#include "core/volume.h"

namespace lumen {

struct VolumeTexture {
    int width = 0;
    int height = 0;
    int depth = 0;
    std::vector<std::uint8_t> voxels; // X-fastest, normalized R8 scalar data

    void clear() {
        width = height = depth = 0;
        voxels.clear();
    }
};

// Convert HU values to a normalized R8 3D texture using the same transfer
// function as ExtractSlice. `max_dimension` controls bounded GPU memory: values
// above it are sampled at an integer stride, preserving physical axis order.
// The returned texture is valid until `out` is reused.
void ExtractVolumeTexture(const Volume& volume, float level, float window,
                         int max_dimension, VolumeTexture& out);

} // namespace lumen
