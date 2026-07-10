// LumenSlice — competitive region growing from multi-label seeds ("grow-cut").
//
// Given a mask that already carries seed strokes for one or more segments (painted
// with the normal tools), grow those seeds outward so every voxel in the seeded
// region is claimed by whichever seed it most resembles in HU. This is the
// automaton of Vezhnevets & Konouchine (grow-cut): each voxel has an owning label
// and a strength in [0,1]; on each pass a voxel can be "conquered" by a neighbour
// whose attack (neighbour strength × HU similarity) beats the voxel's own strength.
//
// Two deliberate scope limits keep it tractable on large scans (see
// docs/engineering/PERFORMANCE.md — touch fewer voxels):
//   * It runs only inside the bounding box of the seeds, expanded by `margin`
//     voxels. Growth cannot escape that box, so paint seeds that bracket the
//     structure you want (and, as in 3D Slicer, a background seed to contain it —
//     without one the whole box is partitioned among the foreground labels).
//   * It is capped at `max_iters` Jacobi passes.
//
// Pure compute: no UI, no threading. The bridge/app decides when to run it.

#pragma once

#include <cstdint>

#include "core/volume.h"
#include "segmentation/label_volume.hpp"

namespace lumen {

// Grow the current seed labels in `mask` competitively over the seeds' bounding box
// (expanded by `margin` voxels, clamped to the volume). `max_iters` bounds the
// number of passes; growth also stops early once a pass changes nothing. Seed
// voxels (any non-zero mask voxel) start at full strength and are never overwritten
// by a weaker claim, so the strokes you painted are preserved. Returns the number
// of voxels whose label changed (newly claimed or reassigned). No-op (returns 0) if
// the mask is empty or carries no seeds.
long grow_from_seeds(const Volume& vol, LabelVolume& mask, int max_iters,
                     int margin);

} // namespace lumen
