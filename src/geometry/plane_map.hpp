// LumenSlice — the single source of truth for slice<->voxel geometry.
//
// Every 2D plane the UI shows is a window onto the flat voxel buffer. The exact
// mapping (which voxel a slice pixel comes from, including the coronal/sagittal
// vertical flip so superior renders toward the top) used to live inline inside
// ExtractSlice. Segmentation needs the SAME mapping in four places: rendering the
// HU slice, rendering the mask overlay, painting onto a slice, and picking a
// voxel from a click. Duplicating the flip is how you silently paint the wrong
// voxel. So it lives here, once, and is unit-tested by a round-trip.
//
//   Plane     out (px,py)            voxel                 out dims
//   Axial     px=x,    py=y          (px, py, index)       width  x height
//   Coronal   px=x,    py=D-1-z      (px, index, D-1-py)   width  x depth
//   Sagittal  px=y,    py=D-1-z      (index, px, D-1-py)   height x depth
//                                    (D = depth)

#pragma once

#include "core/volume.h"

namespace lumen {

// Output image dimensions for a plane (constant per axis for a given volume).
struct SliceDims {
    int width = 0;
    int height = 0;
};

struct VoxelCoord {
    int x = 0;
    int y = 0;
    int z = 0;
};

struct PixelCoord {
    int px = 0;
    int py = 0;
};

// Pixel dimensions of the 2D image produced when slicing `vol` along `axis`.
[[nodiscard]] SliceDims slice_dims(const Volume& vol, Axis axis);

// Map an output-slice pixel (px,py) on `axis` at slice `index` to its voxel.
// No bounds checking on (px,py) — callers iterate within slice_dims().
[[nodiscard]] VoxelCoord plane_to_voxel(const Volume& vol, Axis axis, int index,
                                        int px, int py);

// Inverse: where voxel (x,y,z) lands on the `axis` image. The slice index is
// implied by the voxel's off-axis coordinate; callers that care pass the matching
// index. No bounds checking.
[[nodiscard]] PixelCoord voxel_to_plane(const Volume& vol, Axis axis, int x, int y,
                                        int z);

} // namespace lumen
