// LumenSlice - per-segment quantitative statistics.
//
// Pure, data-oriented measurement over the segmentation. Two independent pieces,
// mirroring the two ways a segment's geometry can be measured:
//
//   1. compute_label_stats() - reads the label map + HU volume directly. Gives the
//      voxel count, the voxel volume, the voxel-face ("staircase") surface area,
//      and the HU distribution (min/max/mean/stddev) over the labelled voxels. One
//      linear pass over the volume.
//
//   2. compute_mesh_metrics() - reads a closed triangle surface (as produced by
//      marching_cubes). Gives the sum-of-triangle-areas surface area and the
//      signed-tetrahedron (divergence-theorem) enclosed volume - the same formulas
//      VTK's vtkMassProperties uses. Both are translation-invariant for a closed
//      manifold, so the caller may march a cropped region without shifting vertices.
//
// This file is core: it depends only on the pure-compute layer (LabelVolume, Volume,
// Mesh) and never on the C bridge or any UI. The bridge orchestrates the marching
// cubes between the two (crop -> march -> compute_mesh_metrics); see
// lumen_bridge_stats.cpp.

#pragma once

#include <cstdint>

#include "core/volume.h"
#include "segmentation/label_volume.hpp"
#include "segmentation/marching_cubes.hpp"

namespace lumen {

// Voxel-derived statistics for one label, computed from the label map + HU volume.
// All fields are 0 when the label has no voxels.
struct SegmentStats {
    long voxel_count = 0;
    double volume_mm3 = 0.0;        // voxel_count * (sx * sy * sz)
    double surface_area_mm2 = 0.0;  // exposed voxel-face area (staircase surface)
    double hu_min = 0.0;
    double hu_max = 0.0;
    double hu_mean = 0.0;
    double hu_stddev = 0.0;         // population standard deviation
};

// Closed-surface metrics from a triangle mesh (vertices in mm). Both are
// translation-invariant for a closed manifold.
struct MeshMetrics {
    double surface_area_mm2 = 0.0;  // sum of triangle areas
    double volume_mm3 = 0.0;        // signed-tetrahedron (divergence) volume, absolute
};

// Measure `label` over `mask` (aligned to `vol`). Counts voxels, sums the exposed
// voxel-face area (a face is exposed when its neighbour across it is not the same
// label - background, a different label, or out of bounds), and accumulates the HU
// distribution in the same pass. Returns zeros when the label is absent.
SegmentStats compute_label_stats(const LabelVolume& mask, const Volume& vol,
                                 std::uint8_t label);

// Measure a closed triangle surface: surface area = sum of triangle areas, volume =
// |(1/6) * sum over triangles of v0 . (v1 x v2)| (divergence theorem). Returns zeros
// for an empty mesh.
MeshMetrics compute_mesh_metrics(const Mesh& mesh);

} // namespace lumen
