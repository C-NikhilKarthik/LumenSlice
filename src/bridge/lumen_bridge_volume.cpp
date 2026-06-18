// LumenSlice C bridge - volume loading, geometry, and slice extraction.
//
// One of three bridge translation units (see also lumen_bridge_segment.cpp and
// lumen_bridge_mesh.cpp). This file is the "load a scan and look at its grayscale
// slices" surface: ingestion, dimensions/spacing/HU range, slice extraction, the
// metadata blob, and the pane-pixel <-> voxel geometry mapping. Each function is a
// thin marshal over the C++ core - no domain logic lives here.

#include "lumen_bridge.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "core/volume.h"
#include "geometry/plane_map.hpp"
#include "io/dicom_loader.h"
#include "lumen_handle.hpp"
#include "visualization/slice_view.h"

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
    handle->editor.reset_to(handle->volume, kDefaultSegmentColor);
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

// Pane-pixel <-> voxel geometry. The single source of truth is plane_map; these
// just expose it across the language line for crosshair + seed/paint mapping.
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

} // extern "C"
