// LumenSlice — voxel segmentation operations (threshold, region grow, paint).
//
// All operations read the HU volume and mutate a LabelVolume. They are pure
// compute (no UI, no threading); the bridge/app decides when to run them and on
// which thread. Paint lands in phase P1b.

#pragma once

#include "core/volume.h"
#include "geometry/plane_map.hpp"
#include "segmentation/label_volume.hpp"

namespace lumen {

// Re-fill the ACTIVE segment from a HU window: every background-or-`label` voxel
// with lo <= HU <= hi becomes `label`, and any `label` voxel now out of range is
// cleared. Voxels owned by OTHER segments are left untouched, so thresholding one
// segment never steals from another. O(voxels). `lo`/`hi` are swapped if reversed.
void threshold_fill(const Volume& vol, float lo, float hi, LabelVolume& mask,
                    std::uint8_t label = kActiveLabel);

// 6-connected flood fill from voxel (sx,sy,sz) into BACKGROUND voxels whose HU is
// within `tol` of the seed voxel's HU; the filled voxels become `label`. Adds to
// the existing mask (does not clear it) and stops at any already-labelled voxel.
// Returns the number of voxels newly labelled. No-op (returns 0) if the seed is
// out of range, or if it already carries a label.
long region_grow(const Volume& vol, int sx, int sy, int sz, float tol,
                 LabelVolume& mask, std::uint8_t label = kActiveLabel);

// Paint or erase a filled disk on one slice plane. The disk is defined in the 2D
// output-pixel space of `axis`/`index` (radius in pixels) and mapped back to
// voxels via plane_map, so it honours the coronal/sagittal flip. Paint (add) sets
// `label`, overwriting whatever was there (the brush wins). Erase (!add) clears
// only voxels that currently carry `label`, so erasing one segment cannot rub out
// another. Returns the number of voxels changed.
long paint_disk(const Volume& vol, Axis axis, int index, int cx, int cy,
                int radius, bool add, LabelVolume& mask,
                std::uint8_t label = kActiveLabel);

// Level tracing on ONE slice: from pixel (cx,cy) of `axis`/`index`, flood the
// 4-connected region of pixels whose HU is at or above the clicked pixel's HU (its
// iso-level) into `label`. This traces a bright structure out to where the image
// drops below the clicked level, so a click selects a whole level set on the slice.
// Stops at voxels owned by another segment. 2D only (the current slice). Returns
// the number of voxels newly labelled; 0 if the seed is out of range or owned by
// another segment.
long level_trace(const Volume& vol, Axis axis, int index, int cx, int cy,
                 LabelVolume& mask, std::uint8_t label = kActiveLabel);

} // namespace lumen
