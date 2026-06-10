#include "lumen_bridge.h"

#include <cstdio>
#include <utility>

#include "core/volume.h"
#include "io/dicom_loader.h"
#include "visualization/slice_view.h"

// Opaque handle: the calibrated volume plus a reusable extraction buffer so the
// Swift side never allocates per frame.
struct LumenVolume {
    lumen::Volume volume;
    lumen::SliceImage scratch;
};

extern "C" {

LumenVolume* lumen_load_folder(const char* path, char* msg, int msg_cap) {
    lumen::LoadResult r = lumen::LoadDicomFolder(path != nullptr ? path : "");
    if (msg != nullptr && msg_cap > 0) {
        std::snprintf(msg, static_cast<size_t>(msg_cap), "%s", r.message.c_str());
    }
    if (!r.ok) return nullptr;

    auto* handle = new LumenVolume();
    handle->volume = std::move(r.volume);
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

} // extern "C"
