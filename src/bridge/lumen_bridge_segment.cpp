// LumenSlice C bridge - segmentation surface.
//
// One of three bridge translation units. Every function here is a one-line marshal
// onto the `SegmentEditor` object that lives inside the handle: the editor owns all
// the domain logic (active label, clearing voxels on remove, undo capture), so this
// file only converts C ints/pointers to and from C++ calls. That separation is the
// whole point of the split - the bridge adapts languages, the editor edits masks.

#include "lumen_bridge.h"

#include <cstdint>

#include "geometry/plane_map.hpp"
#include "lumen_handle.hpp"
#include "segmentation/analysis.hpp"
#include "segmentation/mask_view.hpp"

using lumen_bridge_detail::clamp_u8;

extern "C" {

// --- Segments ---------------------------------------------------------------

int lumen_seg_add(LumenVolume* v, int r, int g, int b) {
    if (v == nullptr) return 0;
    return v->editor.add_segment(
        lumen::Rgb{clamp_u8(r), clamp_u8(g), clamp_u8(b)});
}

void lumen_seg_remove(LumenVolume* v, int id) {
    if (v == nullptr || id <= 0 || id > 255) return;
    v->editor.remove_segment(static_cast<std::uint8_t>(id));
}

int lumen_seg_active(const LumenVolume* v) {
    return v == nullptr ? 0 : v->editor.active();
}

void lumen_seg_set_active(LumenVolume* v, int id) {
    if (v == nullptr || id < 0 || id > 255) return;
    v->editor.set_active(static_cast<std::uint8_t>(id));
}

int lumen_seg_segment_count(const LumenVolume* v) {
    return v == nullptr ? 0 : v->editor.segment_count();
}

int lumen_seg_segment_id_at(const LumenVolume* v, int index) {
    return v == nullptr ? 0 : v->editor.segment_id_at(index);
}

void lumen_seg_set_color(LumenVolume* v, int id, int r, int g, int b) {
    if (v == nullptr || id <= 0 || id > 255) return;
    v->editor.set_color(static_cast<std::uint8_t>(id),
                        lumen::Rgb{clamp_u8(r), clamp_u8(g), clamp_u8(b)});
}

void lumen_seg_get_color(const LumenVolume* v, int id, int* r, int* g, int* b) {
    if (v == nullptr || id <= 0 || id > 255) return;
    const lumen::Rgb c = v->editor.color(static_cast<std::uint8_t>(id));
    if (r) *r = c.r;
    if (g) *g = c.g;
    if (b) *b = c.b;
}

void lumen_seg_set_visible(LumenVolume* v, int id, int visible) {
    if (v == nullptr || id <= 0 || id > 255) return;
    v->editor.set_visible(static_cast<std::uint8_t>(id), visible != 0);
}

int lumen_seg_get_visible(const LumenVolume* v, int id) {
    if (v == nullptr || id <= 0 || id > 255) return 0;
    return v->editor.visible(static_cast<std::uint8_t>(id)) ? 1 : 0;
}

long lumen_seg_label_count(const LumenVolume* v, int id) {
    if (v == nullptr || id <= 0 || id > 255) return 0;
    return v->editor.label_count(static_cast<std::uint8_t>(id));
}

void lumen_seg_label_histogram(const LumenVolume* v, long* out) {
    if (out == nullptr) return;
    for (int i = 0; i < 256; ++i) out[i] = 0;
    if (v == nullptr || !v->editor.mask().valid()) return;
    const lumen::LabelVolume& mask = v->editor.mask();
    const std::uint8_t* data = mask.data();
    const std::size_t n = mask.voxel_count();
    for (std::size_t i = 0; i < n; ++i) ++out[data[i]];
}

// --- Editing operations (act on the active segment) -------------------------

void lumen_seg_threshold(LumenVolume* v, float lo, float hi) {
    if (v == nullptr) return;
    v->editor.threshold(lo, hi);
}

long lumen_seg_region_grow(LumenVolume* v, int x, int y, int z, float tol) {
    if (v == nullptr) return 0;
    return v->editor.region_grow(x, y, z, tol);
}

long lumen_seg_paint(LumenVolume* v, int axis, int index, int cx, int cy,
                     int radius, int add) {
    if (v == nullptr) return 0;
    return v->editor.paint(static_cast<lumen::Axis>(axis), index, cx, cy, radius,
                           add != 0);
}

long lumen_seg_level_trace(LumenVolume* v, int axis, int index, int cx, int cy) {
    if (v == nullptr) return 0;
    return v->editor.level_trace(static_cast<lumen::Axis>(axis), index, cx, cy);
}

void lumen_seg_clear(LumenVolume* v) {
    if (v == nullptr) return;
    v->editor.clear_active();
}

long lumen_seg_count(const LumenVolume* v) {
    return v == nullptr ? 0 : v->editor.total_labelled();
}

float lumen_seg_otsu(const LumenVolume* v) {
    if (v == nullptr) return 0.0f;
    return lumen::otsu_threshold(v->volume);
}

long lumen_seg_keep_largest(LumenVolume* v) {
    return v == nullptr ? 0 : v->editor.keep_largest();
}

long lumen_seg_remove_small(LumenVolume* v, long min_voxels) {
    return v == nullptr ? 0 : v->editor.remove_small(min_voxels);
}

long lumen_seg_grow(LumenVolume* v, int iterations) {
    return v == nullptr ? 0 : v->editor.grow_margin(iterations);
}

long lumen_seg_shrink(LumenVolume* v, int iterations) {
    return v == nullptr ? 0 : v->editor.shrink_margin(iterations);
}

long lumen_seg_smooth(LumenVolume* v, int iterations) {
    return v == nullptr ? 0 : v->editor.smooth(iterations);
}

long lumen_seg_grow_from_seeds(LumenVolume* v, int max_iters) {
    if (v == nullptr) return 0;
    // Fixed 8-voxel margin around the seeds: enough headroom for the grow to reach
    // structure edges without ballooning the working box on a large scan.
    return v->editor.grow_from_seeds(max_iters, 8);
}

long lumen_seg_scissor_cut(LumenVolume* v, const float* mvp, int vp_w, int vp_h,
                           const float* poly_xy, int poly_count, int erase_inside,
                           int only_label) {
    if (v == nullptr) return 0;
    const std::uint8_t label =
        (only_label > 0 && only_label < 256) ? static_cast<std::uint8_t>(only_label)
                                             : 0;
    return v->editor.scissor_cut(mvp, vp_w, vp_h, poly_xy, poly_count,
                                 erase_inside != 0, label);
}

// --- Undo / redo ------------------------------------------------------------

void lumen_seg_push_undo(LumenVolume* v) {
    if (v != nullptr) v->editor.push_undo();
}

int lumen_seg_undo(LumenVolume* v) {
    return (v != nullptr && v->editor.undo()) ? 1 : 0;
}

int lumen_seg_redo(LumenVolume* v) {
    return (v != nullptr && v->editor.redo()) ? 1 : 0;
}

int lumen_seg_can_undo(const LumenVolume* v) {
    return (v != nullptr && v->editor.can_undo()) ? 1 : 0;
}

int lumen_seg_can_redo(const LumenVolume* v) {
    return (v != nullptr && v->editor.can_redo()) ? 1 : 0;
}

// --- Overlay ----------------------------------------------------------------

const unsigned char* lumen_extract_mask_slice(LumenVolume* v, int axis, int index,
                                              int* out_w, int* out_h) {
    if (v == nullptr) return nullptr;
    lumen::ExtractMaskSlice(v->volume, v->editor.mask(), v->editor.segments(),
                            static_cast<lumen::Axis>(axis), index,
                            v->mask_scratch);
    if (out_w) *out_w = v->mask_scratch.width;
    if (out_h) *out_h = v->mask_scratch.height;
    return v->mask_scratch.rgba.empty() ? nullptr : v->mask_scratch.rgba.data();
}

} // extern "C"
