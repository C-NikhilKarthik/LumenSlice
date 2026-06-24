// LumenSlice — render the segmentation mask as a slice overlay.
//
// Produces a premultiplied-RGBA image the same size as ExtractSlice's output for
// the same axis/index: the active colour where the voxel is labelled, fully
// transparent elsewhere. The Swift side draws it over the HU slice with the mask's
// own alpha. Geometry comes from plane_map, so the overlay registers pixel-exact
// with the grayscale slice (same coronal/sagittal flip).

#pragma once

#include "core/volume.h"
#include "segmentation/label_volume.hpp"
#include "segmentation/segment_table.hpp"
#include "visualization/slice_view.h" // SliceImage

namespace lumen {

// Render the mask slice as a premultiplied-RGBA overlay: each voxel painted in its
// segment's colour (from `table`), skipping hidden segments and background. Pixel-
// exact with ExtractSlice for the same axis/index.
void ExtractMaskSlice(const Volume& vol, const LabelVolume& mask,
                      const SegmentTable& table, Axis axis, int index,
                      SliceImage& out);

} // namespace lumen
