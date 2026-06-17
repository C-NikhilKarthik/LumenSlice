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

// Replace the entire mask: label kActiveLabel where lo <= HU <= hi, else 0.
// O(voxels). `lo`/`hi` are swapped if given reversed.
void threshold_fill(const Volume& vol, float lo, float hi, LabelVolume& mask);

// 6-connected flood fill from voxel (sx,sy,sz) into BACKGROUND voxels whose HU is
// within `tol` of the seed voxel's HU. Adds to the existing mask (does not clear
// it). Returns the number of voxels newly labelled. No-op (returns 0) if the seed
// is out of range. Bounded by the volume — visited voxels become labelled, so the
// fill cannot revisit and cannot exceed voxel_count().
long region_grow(const Volume& vol, int sx, int sy, int sz, float tol,
                 LabelVolume& mask);

// Paint or erase a filled disk on one slice plane (P1b). The disk is defined in
// the 2D output-pixel space of `axis`/`index` (radius in pixels) and mapped back
// to voxels via plane_map, so it honours the coronal/sagittal flip. Returns the
// number of voxels changed.
long paint_disk(const Volume& vol, Axis axis, int index, int cx, int cy,
                int radius, bool add, LabelVolume& mask);

} // namespace lumen
