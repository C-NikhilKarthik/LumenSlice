#include "lumen_bridge.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include <cerrno>
#include <cstdint>
#include <vector>

#include "core/volume.h"
#include "geometry/plane_map.hpp"
#include "io/dicom_loader.h"
#include "segmentation/analysis.hpp"
#include "segmentation/label_volume.hpp"
#include "segmentation/marching_cubes.hpp"
#include "segmentation/mask_view.hpp"
#include "segmentation/segment.hpp"
#include "segmentation/segment_table.hpp"
#include "segmentation/stl_export.hpp"
#include "segmentation/undo_stack.hpp"
#include "visualization/slice_view.h"

// Opaque handle: the calibrated volume, reusable extraction buffers so the Swift
// side never allocates per frame, the serialized metadata blob (computed once at
// load), the segmentation mask + overlay scratch, and the 3D surface mesh plus a
// mask snapshot the mesh is generated from (so generation can run off-thread
// without racing the live mask).
struct LumenVolume {
    lumen::Volume volume;
    lumen::SliceImage scratch;
    lumen::SliceImage mask_scratch;
    lumen::LabelVolume mask;
    lumen::SegmentTable segments;
    lumen::UndoStack undo;
    std::string meta_json;

    lumen::Mesh mesh;
    std::vector<std::uint8_t> mesh_snapshot;
    int snap_w = 0;
    int snap_h = 0;
    int snap_d = 0;
};

namespace {
// Colour of the segment auto-created on load (a cyan-teal, echoing the original
// single-label overlay). The UI overrides colours via lumen_seg_set_color.
constexpr lumen::Rgb kDefaultSegmentColor{0, 180, 210};
} // namespace

extern "C" {

LumenVolume* lumen_load_folder(const char* path, char* msg, int msg_cap) {
    lumen::LoadResult r = lumen::LoadDicomFolder(path != nullptr ? path : "");
    if (msg != nullptr && msg_cap > 0) {
        std::snprintf(msg, static_cast<size_t>(msg_cap), "%s", r.message.c_str());
    }
    if (!r.ok) return nullptr;

    auto* handle = new LumenVolume();
    handle->volume = std::move(r.volume);
    handle->mask.reset_to(handle->volume); // empty mask aligned to the volume
    handle->segments.clear();
    handle->segments.add(kDefaultSegmentColor); // one active segment to start
    handle->undo.reset();
    handle->meta_json = lumen::serialize_meta_json(r.meta, r.tags);
    return handle;
}

void lumen_free(LumenVolume* v) { delete v; }

void lumen_dims(const LumenVolume* v, int* w, int* h, int* d) {
    if (v == nullptr) return;
    if (w) *w = v->volume.width;
    if (h) *h = v->volume.height;
    if (d) *d = v->volume.depth;
}

void lumen_spacing(const LumenVolume* v, float* sx, float* sy, float* sz) {
    if (v == nullptr) return;
    if (sx) *sx = v->volume.spacing_x;
    if (sy) *sy = v->volume.spacing_y;
    if (sz) *sz = v->volume.spacing_z;
}

void lumen_hu_range(const LumenVolume* v, float* lo, float* hi) {
    if (v == nullptr) return;
    if (lo) *lo = v->volume.hu_min;
    if (hi) *hi = v->volume.hu_max;
}

int lumen_slice_count(const LumenVolume* v, int axis) {
    if (v == nullptr) return 0;
    return v->volume.slice_count(static_cast<lumen::Axis>(axis));
}

void lumen_slice_dims(const LumenVolume* v, int axis, int* w, int* h) {
    if (v == nullptr) return;
    const lumen::Volume& vol = v->volume;
    switch (static_cast<lumen::Axis>(axis)) {
    case lumen::Axis::Axial:
        if (w) *w = vol.width;
        if (h) *h = vol.height;
        break;
    case lumen::Axis::Coronal:
        if (w) *w = vol.width;
        if (h) *h = vol.depth;
        break;
    case lumen::Axis::Sagittal:
        if (w) *w = vol.height;
        if (h) *h = vol.depth;
        break;
    }
}

float lumen_sample_hu(const LumenVolume* v, int x, int y, int z) {
    if (v == nullptr || !v->volume.valid()) return 0.0f;
    const lumen::Volume& vol = v->volume;
    if (x < 0 || y < 0 || z < 0 || x >= vol.width || y >= vol.height ||
        z >= vol.depth) {
        return 0.0f;
    }
    return vol.voxel_buffer[vol.index(x, y, z)];
}

const unsigned char* lumen_extract_slice(LumenVolume* v, int axis, int index,
                                         float level, float window, int* out_w,
                                         int* out_h) {
    if (v == nullptr) return nullptr;
    lumen::ExtractSlice(v->volume, static_cast<lumen::Axis>(axis), index, level,
                        window, v->scratch);
    if (out_w) *out_w = v->scratch.width;
    if (out_h) *out_h = v->scratch.height;
    return v->scratch.rgba.empty() ? nullptr : v->scratch.rgba.data();
}

int lumen_meta_json(const LumenVolume* v, char* out, int out_cap) {
    if (v == nullptr) {
        if (out != nullptr && out_cap > 0) out[0] = '\0';
        return 0;
    }
    const std::string& json = v->meta_json;
    const int full_len = static_cast<int>(json.size());
    if (out != nullptr && out_cap > 0) {
        const int copy_len = std::min(full_len, out_cap - 1);
        std::memcpy(out, json.data(), static_cast<size_t>(copy_len));
        out[copy_len] = '\0';
    }
    return full_len;
}

// --- Segmentation -----------------------------------------------------------

namespace {
std::uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<std::uint8_t>(v);
}
} // namespace

int lumen_seg_add(LumenVolume* v, int r, int g, int b) {
    if (v == nullptr) return 0;
    return v->segments.add(
        lumen::Rgb{clamp_u8(r), clamp_u8(g), clamp_u8(b)});
}

void lumen_seg_remove(LumenVolume* v, int id) {
    if (v == nullptr || id <= 0 || id > 255) return;
    const std::uint8_t label = static_cast<std::uint8_t>(id);
    // Drop its voxels from the mask, then forget the metadata.
    std::uint8_t* data = v->mask.data();
    const std::size_t n = v->mask.voxel_count();
    for (std::size_t i = 0; i < n; ++i)
        if (data[i] == label) data[i] = 0;
    v->segments.remove(label);
}

int lumen_seg_active(const LumenVolume* v) {
    return v == nullptr ? 0 : v->segments.active();
}

void lumen_seg_set_active(LumenVolume* v, int id) {
    if (v == nullptr || id < 0 || id > 255) return;
    v->segments.set_active(static_cast<std::uint8_t>(id));
}

int lumen_seg_segment_count(const LumenVolume* v) {
    return v == nullptr ? 0 : v->segments.count();
}

int lumen_seg_segment_id_at(const LumenVolume* v, int index) {
    return v == nullptr ? 0 : v->segments.id_at(index);
}

void lumen_seg_set_color(LumenVolume* v, int id, int r, int g, int b) {
    if (v == nullptr || id <= 0 || id > 255) return;
    v->segments.set_color(static_cast<std::uint8_t>(id),
                          lumen::Rgb{clamp_u8(r), clamp_u8(g), clamp_u8(b)});
}

void lumen_seg_get_color(const LumenVolume* v, int id, int* r, int* g, int* b) {
    if (v == nullptr || id <= 0 || id > 255) return;
    const lumen::Rgb c = v->segments.color(static_cast<std::uint8_t>(id));
    if (r) *r = c.r;
    if (g) *g = c.g;
    if (b) *b = c.b;
}

void lumen_seg_set_visible(LumenVolume* v, int id, int visible) {
    if (v == nullptr || id <= 0 || id > 255) return;
    v->segments.set_visible(static_cast<std::uint8_t>(id), visible != 0);
}

int lumen_seg_get_visible(const LumenVolume* v, int id) {
    if (v == nullptr || id <= 0 || id > 255) return 0;
    return v->segments.visible(static_cast<std::uint8_t>(id)) ? 1 : 0;
}

long lumen_seg_label_count(const LumenVolume* v, int id) {
    if (v == nullptr || id <= 0 || id > 255) return 0;
    const std::uint8_t label = static_cast<std::uint8_t>(id);
    const std::uint8_t* data = v->mask.data();
    const std::size_t n = v->mask.voxel_count();
    long count = 0;
    for (std::size_t i = 0; i < n; ++i)
        if (data[i] == label) ++count;
    return count;
}

void lumen_seg_threshold(LumenVolume* v, float lo, float hi) {
    if (v == nullptr) return;
    lumen::threshold_fill(v->volume, lo, hi, v->mask, v->segments.active());
}

long lumen_seg_region_grow(LumenVolume* v, int x, int y, int z, float tol) {
    if (v == nullptr) return 0;
    return lumen::region_grow(v->volume, x, y, z, tol, v->mask,
                              v->segments.active());
}

long lumen_seg_paint(LumenVolume* v, int axis, int index, int cx, int cy,
                     int radius, int add) {
    if (v == nullptr) return 0;
    return lumen::paint_disk(v->volume, static_cast<lumen::Axis>(axis), index, cx,
                             cy, radius, add != 0, v->mask,
                             v->segments.active());
}

void lumen_seg_clear(LumenVolume* v) {
    if (v == nullptr) return;
    const std::uint8_t label = v->segments.active();
    if (label == 0) return;
    std::uint8_t* data = v->mask.data();
    const std::size_t n = v->mask.voxel_count();
    for (std::size_t i = 0; i < n; ++i)
        if (data[i] == label) data[i] = 0;
}

long lumen_seg_count(const LumenVolume* v) {
    if (v == nullptr) return 0;
    return v->mask.count_nonzero();
}

float lumen_seg_otsu(const LumenVolume* v) {
    if (v == nullptr) return 0.0f;
    return lumen::otsu_threshold(v->volume);
}

long lumen_seg_keep_largest(LumenVolume* v) {
    if (v == nullptr) return 0;
    return lumen::keep_largest_island(v->mask, v->segments.active());
}

long lumen_seg_remove_small(LumenVolume* v, long min_voxels) {
    if (v == nullptr) return 0;
    return lumen::remove_small_islands(v->mask, v->segments.active(), min_voxels);
}

void lumen_seg_push_undo(LumenVolume* v) {
    if (v == nullptr) return;
    v->undo.capture(v->mask);
}

int lumen_seg_undo(LumenVolume* v) {
    if (v == nullptr) return 0;
    return v->undo.undo(v->mask) ? 1 : 0;
}

int lumen_seg_redo(LumenVolume* v) {
    if (v == nullptr) return 0;
    return v->undo.redo(v->mask) ? 1 : 0;
}

int lumen_seg_can_undo(const LumenVolume* v) {
    return (v != nullptr && v->undo.can_undo()) ? 1 : 0;
}

int lumen_seg_can_redo(const LumenVolume* v) {
    return (v != nullptr && v->undo.can_redo()) ? 1 : 0;
}

const unsigned char* lumen_extract_mask_slice(LumenVolume* v, int axis, int index,
                                              int* out_w, int* out_h) {
    if (v == nullptr) return nullptr;
    lumen::ExtractMaskSlice(v->volume, v->mask, v->segments,
                            static_cast<lumen::Axis>(axis), index,
                            v->mask_scratch);
    if (out_w) *out_w = v->mask_scratch.width;
    if (out_h) *out_h = v->mask_scratch.height;
    return v->mask_scratch.rgba.empty() ? nullptr : v->mask_scratch.rgba.data();
}

void lumen_slice_pixel_to_voxel(const LumenVolume* v, int axis, int index, int px,
                                int py, int* x, int* y, int* z) {
    if (v == nullptr) return;
    const lumen::VoxelCoord c = lumen::plane_to_voxel(
        v->volume, static_cast<lumen::Axis>(axis), index, px, py);
    if (x) *x = c.x;
    if (y) *y = c.y;
    if (z) *z = c.z;
}

void lumen_voxel_to_slice_pixel(const LumenVolume* v, int axis, int x, int y, int z,
                                int* px, int* py) {
    if (v == nullptr) return;
    const lumen::PixelCoord p = lumen::voxel_to_plane(
        v->volume, static_cast<lumen::Axis>(axis), x, y, z);
    if (px) *px = p.px;
    if (py) *py = p.py;
}

// --- 3D surface (marching cubes) --------------------------------------------

void lumen_mesh_snapshot(LumenVolume* v) {
    if (v == nullptr || !v->mask.valid()) return;
    v->snap_w = v->mask.width();
    v->snap_h = v->mask.height();
    v->snap_d = v->mask.depth();
    v->mesh_snapshot.assign(v->mask.data(),
                            v->mask.data() + v->mask.voxel_count());
}

void lumen_mesh_snapshot_label(LumenVolume* v, int id) {
    if (v == nullptr || !v->mask.valid() || id <= 0 || id > 255) return;
    const std::uint8_t label = static_cast<std::uint8_t>(id);
    v->snap_w = v->mask.width();
    v->snap_h = v->mask.height();
    v->snap_d = v->mask.depth();
    const std::size_t n = v->mask.voxel_count();
    const std::uint8_t* src = v->mask.data();
    v->mesh_snapshot.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        v->mesh_snapshot[i] = (src[i] == label) ? 1 : 0; // binarize this segment
}

int lumen_mesh_generate(LumenVolume* v, int smooth_iters, int downsample) {
    if (v == nullptr || v->mesh_snapshot.empty()) return 0;
    return lumen::marching_cubes(v->mesh_snapshot.data(), v->snap_w, v->snap_h,
                                 v->snap_d, v->volume.spacing_x,
                                 v->volume.spacing_y, v->volume.spacing_z,
                                 smooth_iters, downsample, v->mesh);
}

int lumen_mesh_vertex_count(const LumenVolume* v) {
    return v == nullptr ? 0 : v->mesh.vertex_count();
}

int lumen_mesh_index_count(const LumenVolume* v) {
    return v == nullptr ? 0 : static_cast<int>(v->mesh.indices.size());
}

const float* lumen_mesh_vertices(const LumenVolume* v) {
    if (v == nullptr || v->mesh.vertices.empty()) return nullptr;
    return v->mesh.vertices.data();
}

const float* lumen_mesh_normals(const LumenVolume* v) {
    if (v == nullptr || v->mesh.normals.empty()) return nullptr;
    return v->mesh.normals.data();
}

const unsigned int* lumen_mesh_indices(const LumenVolume* v) {
    if (v == nullptr || v->mesh.indices.empty()) return nullptr;
    return v->mesh.indices.data();
}

int lumen_mesh_write_stl(const LumenVolume* v, const char* path) {
    if (v == nullptr) return EINVAL;
    return lumen::write_binary_stl(v->mesh, path);
}

} // extern "C"
