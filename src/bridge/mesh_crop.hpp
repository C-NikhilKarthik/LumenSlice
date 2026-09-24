// LumenSlice - bridge-private label-region cropping, shared by the 3D snapshot and
// the statistics closed-surface path.
//
// Both need the same thing before marching cubes: the labelled region binarized to
// a 0/1 field, cropped to its bounding box plus a one-voxel zero margin (so the
// field has a closing border), together with the crop's origin in the full volume.
// Factoring it here keeps the two callers byte-for-byte identical and lets the stats
// path crop into its OWN buffer without disturbing the handle's live mesh snapshot.
//
// This header lives under src/bridge (not bridge/include): it is internal to the
// bridge translation units and pulls in a C++ core type (LabelVolume), so it never
// crosses the C language line.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "segmentation/label_volume.hpp"

namespace lumen_bridge_detail {

// Geometry of a crop: the cropped dimensions and the origin (in full-volume voxels)
// of the cropped box. w == 0 means nothing matched.
struct CropRegion {
    int w = 0, h = 0, d = 0;
    int ox = 0, oy = 0, oz = 0;
};

// Crop `mask` to the voxels for which keep(id) is true, binarized to 0/1 into `out`,
// ALWAYS surrounded by a one-voxel zero border. Returns the cropped geometry; when
// nothing matches, clears `out` and returns a zero region.
//
// The zero border is unconditional - even when the labelled region touches the
// volume edge, we pad rather than clamp (matching 3D Slicer's vtkImageConstantPad).
// Without it, a structure reaching the scan boundary would have no closing face
// there, so marching cubes produced an OPEN surface and any enclosed-volume measure
// (or a watertight STL) leaked. The origin may therefore be -1 (one voxel outside
// the volume); that is fine - surface area and the divergence-theorem volume are
// translation-invariant, and the 3D path shifts vertices by origin * spacing.
template <class KeepFn>
CropRegion crop_label_region(const lumen::LabelVolume& mask, KeepFn keep,
                             std::vector<std::uint8_t>& out) {
    const int W = mask.width(), H = mask.height(), D = mask.depth();
    const std::uint8_t* src = mask.data();

    // One linear pass to find the inclusive bounding box of kept voxels.
    int x0 = W, y0 = H, z0 = D, x1 = -1, y1 = -1, z1 = -1;
    for (int z = 0; z < D; ++z) {
        for (int y = 0; y < H; ++y) {
            const std::uint8_t* row =
                src + (static_cast<std::size_t>(z) * H + y) * W;
            for (int x = 0; x < W; ++x) {
                if (!keep(row[x])) continue;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
                if (z < z0) z0 = z;
                if (z > z1) z1 = z;
            }
        }
    }

    CropRegion r;
    if (x1 < 0) { // nothing labelled
        out.clear();
        return r;
    }

    // Cropped box = the bounding box plus a one-voxel zero border on every side. The
    // origin is bbox_min - 1 (may be -1); the border planes stay 0 from the fill.
    r.ox = x0 - 1;
    r.oy = y0 - 1;
    r.oz = z0 - 1;
    r.w = (x1 - x0 + 1) + 2;
    r.h = (y1 - y0 + 1) + 2;
    r.d = (z1 - z0 + 1) + 2;
    out.assign(static_cast<std::size_t>(r.w) * r.h * r.d, 0);

    // Copy the kept voxels into the interior (indices >= 1 on every axis).
    for (int z = z0; z <= z1; ++z) {
        for (int y = y0; y <= y1; ++y) {
            const std::uint8_t* row =
                src + (static_cast<std::size_t>(z) * H + y) * W;
            const int lz = z - r.oz, ly = y - r.oy;
            std::uint8_t* dst =
                out.data() + (static_cast<std::size_t>(lz) * r.h + ly) * r.w;
            for (int x = x0; x <= x1; ++x)
                if (keep(row[x])) dst[x - r.ox] = 1;
        }
    }
    return r;
}

} // namespace lumen_bridge_detail
