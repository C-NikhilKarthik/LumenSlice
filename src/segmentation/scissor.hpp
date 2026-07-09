// LumenSlice — 3D scissor: cut a segmentation by a screen-space lasso.
//
// The user draws a freehand outline over the 3D surface; every labelled voxel that
// projects inside that outline (through the full depth) is erased. The projection
// happens here, in the core, because it is a per-labelled-voxel loop — doing it in
// Swift would mean marshalling millions of points across the bridge.
//
// The caller supplies the combined view·projection matrix SceneKit is using, in
// ROW-MAJOR order with the row-vector convention SceneKit itself uses (translation
// in the 4th row). A world point (in millimetres) projects as:
//
//     clip_j = Σ_i world_i · mvp[i*4 + j]      (world = [x·sx, y·sy, z·sz, 1])
//     ndc    = clip.xy / clip.w                 (requires clip.w > 0: in front)
//     screen = ((ndc.x·0.5+0.5)·vp_w, (1 − (ndc.y·0.5+0.5))·vp_h)
//
// i.e. `screen` is in top-left-origin, y-down pixels — the same space the lasso
// polygon is captured in (the overlay NSView is flipped). Keeping both in one space
// is what makes the cut line up with what is drawn.
//
// Pure compute: no UI, no threading.

#pragma once

#include <cstdint>

#include "core/volume.h"
#include "segmentation/label_volume.hpp"

namespace lumen {

// Erase (set to background) every labelled voxel that projects INSIDE the lasso
// when `erase_inside` is true, or OUTSIDE it when false. `mvp` is 16 floats (see
// header comment); `poly_xy` is `poly_count` (x,y) screen-pixel pairs = 2·count
// floats. `only_label` limits the cut to that segment id, or 0 to cut every
// labelled voxel. Returns the number of voxels cleared. No-op for a degenerate
// polygon (< 3 points) or a null/empty mask.
long scissor_cut(const Volume& vol, LabelVolume& mask, const float* mvp, int vp_w,
                 int vp_h, const float* poly_xy, int poly_count, bool erase_inside,
                 std::uint8_t only_label);

} // namespace lumen
