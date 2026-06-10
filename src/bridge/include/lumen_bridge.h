// LumenSlice — C bridge over the C++ core.
//
// A pure-C surface (no C++ types leak out) so Swift can `import LumenCore` and
// drive ingestion + slice extraction. The heavy lifting stays in src/core,
// src/io, src/visualization — this file only marshals across the language line,
// honouring the UI/data isolation rule in docs/agent.md §1.

#ifndef LUMEN_BRIDGE_H
#define LUMEN_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to a loaded volume (wraps lumen::Volume + a scratch slice).
typedef struct LumenVolume LumenVolume;

// Axis selectors, matching lumen::Axis.
enum { LUMEN_AXIS_AXIAL = 0, LUMEN_AXIS_CORONAL = 1, LUMEN_AXIS_SAGITTAL = 2 };

// Load every usable DICOM slice under `path` into one calibrated volume.
// Returns NULL on failure. `msg`/`msg_cap` (optional) receive a status string.
LumenVolume* lumen_load_folder(const char* path, char* msg, int msg_cap);

// Release a handle returned by lumen_load_folder (NULL is ignored).
void lumen_free(LumenVolume* v);

// Volume geometry.
void lumen_dims(const LumenVolume* v, int* w, int* h, int* d);
void lumen_spacing(const LumenVolume* v, float* sx, float* sy, float* sz);
void lumen_hu_range(const LumenVolume* v, float* lo, float* hi);

// Number of slices when scrolling along `axis`.
int lumen_slice_count(const LumenVolume* v, int axis);

// Pixel dimensions of a slice on `axis` (constant per axis for a volume).
void lumen_slice_dims(const LumenVolume* v, int axis, int* w, int* h);

// Single HU sample (mainly for testing/inspection). 0 if out of range.
float lumen_sample_hu(const LumenVolume* v, int x, int y, int z);

// Extract slice `index` along `axis`, mapped through window/level. Returns a
// pointer to an internal RGBA8 buffer (out_w * out_h * 4 bytes), valid until the
// next extract call on the same handle. Returns NULL on error.
const unsigned char* lumen_extract_slice(LumenVolume* v, int axis, int index,
                                         float level, float window, int* out_w,
                                         int* out_h);

#ifdef __cplusplus
}
#endif

#endif // LUMEN_BRIDGE_H
