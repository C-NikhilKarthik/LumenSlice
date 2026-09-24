// LumenSlice C bridge - per-segment statistics.
//
// One of the bridge translation units. Orchestrates the two halves of a segment
// measurement across the language line into a flat double[] the Swift/Qt frontends
// index by LUMEN_STAT_*:
//   - voxel-derived measures come straight from the core (compute_label_stats);
//   - closed-surface measures need a surface first, so this file crops the label
//     (shared mesh_crop.hpp) and marches it (marching_cubes) into a LOCAL mesh, then
//     hands that to the core (compute_mesh_metrics).
// The measurement reads the frozen stats snapshot (lumen_seg_stats_snapshot, taken on
// the main thread) plus the immutable HU volume, and writes no shared handle buffers,
// so it is safe to run off the main thread while the handle is pinned - it never
// touches the live mask or the 3D mesh buffers.

#include "lumen_bridge.h"

#include <cstdint>
#include <vector>

#include "lumen_handle.hpp"
#include "mesh_crop.hpp"
#include "segmentation/marching_cubes.hpp"
#include "segmentation/statistics.hpp"

extern "C" {

void lumen_seg_stats_snapshot(LumenVolume* v) {
    if (v == nullptr || !v->editor.mask().valid()) return;
    // Deep-copy the live mask on the caller's (main) thread. The subsequent
    // lumen_seg_stats reads only this frozen copy, so a concurrent main-thread edit
    // (paint, threshold, undo, ...) can never race the background measurement.
    v->stats_mask = v->editor.mask();
}

void lumen_seg_stats(const LumenVolume* v, int id, double* out) {
    if (out == nullptr) return;
    for (int i = 0; i < LUMEN_STAT_COUNT; ++i) out[i] = 0.0;
    if (v == nullptr || id <= 0 || id > 255) return;

    const std::uint8_t label = static_cast<std::uint8_t>(id);
    // Measure the frozen snapshot, never the live mask (see lumen_seg_stats_snapshot).
    const lumen::LabelVolume& mask = v->stats_mask;
    if (!mask.valid()) return;

    // Voxel-derived measures (one pass over the volume). An absent label yields a
    // zero voxel_count; leave every field zero-filled in that case.
    const lumen::SegmentStats s = lumen::compute_label_stats(mask, v->volume, label);
    if (s.voxel_count == 0) return;

    out[LUMEN_STAT_VOXEL_COUNT] = static_cast<double>(s.voxel_count);
    out[LUMEN_STAT_VOLUME_MM3] = s.volume_mm3;  // label-map volume (the accurate one)
    out[LUMEN_STAT_HU_MIN] = s.hu_min;
    out[LUMEN_STAT_HU_MAX] = s.hu_max;
    out[LUMEN_STAT_HU_MEAN] = s.hu_mean;
    out[LUMEN_STAT_HU_STDDEV] = s.hu_stddev;

    // Closed-surface volume + area from a marching-cubes surface of this label. Crop
    // into a local field (own buffer, so the handle's live 3D snapshot is untouched;
    // crop_label_region always pads a zero border so the surface closes even at the
    // volume edge - the fix that made the enclosed volume faithful) and march at full
    // resolution with light smoothing, matching 3D Slicer's closed-surface pipeline.
    // Calibrated against Slicer on a whole-body bone segment (identical voxel count):
    //   ours   Volume(CS) 2977 cm3, Surface 18076 cm2
    //   Slicer Volume(CS) 3061 cm3, Surface 18462 cm2   (within ~2-3%; Volume(LM)
    // matches exactly). Two blur passes bring the stair-step raw area (24132 cm2)
    // down to a clinical closed-surface area.
    constexpr int kStatsSurfaceSmoothing = 2;
    std::vector<std::uint8_t> field;
    const auto region = lumen_bridge_detail::crop_label_region(
        mask, [label](std::uint8_t x) { return x == label; }, field);
    if (region.w > 0) {
        lumen::Mesh mesh;
        const int tris = lumen::marching_cubes(
            field.data(), region.w, region.h, region.d, v->volume.spacing_x,
            v->volume.spacing_y, v->volume.spacing_z, kStatsSurfaceSmoothing,
            /*downsample=*/1, mesh);
        if (tris > 0) {
            const lumen::MeshMetrics m = lumen::compute_mesh_metrics(mesh);
            out[LUMEN_STAT_SURFACE_AREA_MM2] = m.surface_area_mm2;
            out[LUMEN_STAT_MESH_VOLUME_MM3] = m.volume_mm3;
        }
    }
}

} // extern "C"
