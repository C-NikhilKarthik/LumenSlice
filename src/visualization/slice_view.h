// LumenSlice — slice extraction for the tri-axis 2D views.
//
// Samples one orthographic plane out of the flat voxel buffer (docs/context.md
// §1.2) and applies a window/level transfer function to produce an 8-bit RGBA
// image ready to upload as a GPU texture via sg_update_image.

#pragma once

#include <cstdint>
#include <vector>

#include "core/volume.h"

namespace lumen {

// A CPU-side grayscale image, packed RGBA8 (gray in R/G/B, A=255).
struct SliceImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; // width * height * 4
};

// Extract slice `index` along `axis`, mapping HU through the window/level
// transfer function (center = level, full width = window). The result is
// resized to fit. `index` is clamped to the valid range for that axis.
void ExtractSlice(const Volume& vol, Axis axis, int index, float level,
                  float window, SliceImage& out);

} // namespace lumen
