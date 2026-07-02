// LumenSlice — segmentation analysis helpers (auto-threshold + island cleanup).
//
// Pure compute over a Volume / LabelVolume, no UI and no threading. These back the
// Segment Editor's "Otsu" auto-threshold and the "Islands" cleanup effects.

#pragma once

#include <cstdint>

#include "core/volume.h"
#include "segmentation/label_volume.hpp"

namespace lumen {

// Otsu's method over the HU histogram: the threshold that maximises between-class
// variance. Returns the HU value separating the (darker) background from the
// (brighter) foreground; the UI typically labels [otsu, hu_max]. Returns hu_min
// for a degenerate/empty volume.
float otsu_threshold(const Volume& vol);

// Keep only the largest 6-connected component of voxels equal to `label`; clear
// the rest to background. Returns the number of voxels removed. No-op on an empty
// label.
long keep_largest_island(LabelVolume& mask, std::uint8_t label);

// Remove every 6-connected component of `label` voxels smaller than `min_voxels`.
// Returns the number of voxels removed.
long remove_small_islands(LabelVolume& mask, std::uint8_t label, long min_voxels);

// Margin: grow (dilate) the `label` region outward by `iterations` 6-connected
// layers, claiming only BACKGROUND voxels (never another segment). Returns voxels
// added.
long dilate_label(LabelVolume& mask, std::uint8_t label, int iterations);

// Margin: shrink (erode) the `label` region inward by `iterations` layers (any
// label voxel touching a non-label voxel is peeled off). Returns voxels removed.
long erode_label(LabelVolume& mask, std::uint8_t label, int iterations);

// Smooth: a 26-neighbourhood majority (median) filter applied `iterations` times.
// Rounds off jagged paint and fills pinholes; claims only background and clears
// only `label` (other segments are untouched). Returns voxels changed.
long smooth_label(LabelVolume& mask, std::uint8_t label, int iterations);

// Grow-from-seeds (marker-controlled watershed). Treat every currently-labelled
// voxel as a seed and assign each reachable BACKGROUND voxel to the label whose
// seed reaches it along the path of least maximum HU step (a minimax / bottleneck
// path), so segments compete along intensity edges. Existing labels are never
// overwritten. Meaningful results need >= 2 distinct seed labels (e.g. structure
// + background).
//
// `tolerance` (HU) bounds the grow: a voxel is only claimed if the worst step on
// its cheapest path stays <= tolerance, so growth stops at strong intensity edges
// and dissimilar background is left unlabelled. Calling again expands another band
// from the enlarged regions (the iterative "grow / update" workflow). A tolerance
// <= 0 fills every reachable voxel. Runs in O(voxels) via a bucketed cost queue.
// Returns the number of voxels newly labelled; a no-op (0) when there are no seeds.
long grow_from_seeds(const Volume& vol, LabelVolume& mask, float tolerance);

} // namespace lumen
