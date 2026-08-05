// LumenSlice — competitive region growing from multi-label seeds ("grow-cut").
//
// Given a mask that already carries seed strokes for one or more segments (painted
// with the normal tools), grow those seeds outward so every voxel in the seeded
// region is claimed by the lowest-cost physical path from a seed. This is the
// FastGrowCut/Dijkstra formulation used by current 3D Slicer.
//
// Two deliberate scope limits keep it tractable on large scans (see
// docs/engineering/PERFORMANCE.md — touch fewer voxels):
//   * It runs only inside the bounding box of the seeds, expanded by `margin`
//     voxels. Growth cannot escape that box, so paint seeds that bracket the
//     structure you want (and, as in 3D Slicer, a background seed to contain it —
//     without one the expanded region is partitioned among the foreground labels).
//   * The legacy `max_iters` argument is retained for API compatibility; the heap
//     runs to convergence like Slicer's FastGrowCut implementation.
//
// `margin` is an explicit voxel margin for tests and low-level callers. A negative
// margin selects the Slicer-style extent rule when negative: each axis expands by
// 50% of the combined seed extent, with a minimum of 3 voxels. A margin of -2
// selects the entire volume for applications that explicitly request full-volume
// growth.
// `distance_penalty` is the Slicer-style seed locality control: increasing it
// makes seed influence fall off faster with physical distance.

#pragma once

#include <cstdint>

#include "core/volume.h"
#include "segmentation/label_volume.hpp"

namespace lumen {

// Grow the current seed labels in `mask` competitively over the seeds' bounding box
// (expanded by `margin` voxels, clamped to the volume). Seed voxels (any non-zero
// mask voxel) are fixed sources, so the strokes you painted are preserved. Returns
// the number of voxels whose label changed (newly claimed or reassigned). No-op if
// the mask is empty or carries no seeds.
long grow_from_seeds(const Volume& vol, LabelVolume& mask, int max_iters,
                     int margin, float distance_penalty = 0.0f);

} // namespace lumen
