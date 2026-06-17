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

// Curated patient/study/series metadata plus the full top-level tag list,
// serialized as one JSON object: {"meta":{...},"tags":[{"ge","vr","name","value"}]}.
// Writes up to out_cap bytes (always NUL-terminated when out_cap > 0) into out,
// and returns the full JSON length in bytes (excluding the NUL). If the return
// value is >= out_cap, the JSON was truncated: call again with a buffer of at
// least (returned length + 1). Returns 0 when there is no metadata.
int lumen_meta_json(const LumenVolume* v, char* out, int out_cap);

// Extract slice `index` along `axis`, mapped through window/level. Returns a
// pointer to an internal RGBA8 buffer (out_w * out_h * 4 bytes), valid until the
// next extract call on the same handle. Returns NULL on error.
const unsigned char* lumen_extract_slice(LumenVolume* v, int axis, int index,
                                         float level, float window, int* out_w,
                                         int* out_h);

// --- Segmentation -----------------------------------------------------------
// All operations act on the single active label. The mask is allocated to match
// the volume on load and reset on each new load.

// Replace the whole mask: label every voxel whose HU is in [lo, hi].
void lumen_seg_threshold(LumenVolume* v, float lo, float hi);

// 6-connected flood fill from voxel (x,y,z) into background voxels within `tol`
// HU of the seed; adds to the mask. Returns the number of voxels newly labelled.
long lumen_seg_region_grow(LumenVolume* v, int x, int y, int z, float tol);

// Paint (add != 0) or erase (add == 0) a filled disk of `radius` slice-pixels on
// the given plane. Returns the number of voxels changed. (Wired for P1b.)
long lumen_seg_paint(LumenVolume* v, int axis, int index, int cx, int cy,
                     int radius, int add);

// Clear the whole mask to background.
void lumen_seg_clear(LumenVolume* v);

// Number of labelled voxels (for the UI / enabling the 3D step).
long lumen_seg_count(const LumenVolume* v);

// Colored mask overlay for slice `index` on `axis`: premultiplied RGBA8, the same
// dimensions and orientation as lumen_extract_slice, transparent where unlabelled.
// Returns a pointer to an internal buffer valid until the next mask-slice extract
// on the same handle. Returns NULL on error.
const unsigned char* lumen_extract_mask_slice(LumenVolume* v, int axis, int index,
                                              int* out_w, int* out_h);

// Map a displayed slice pixel (px,py) on `axis`/`index` to a voxel coordinate.
// One source of truth for the coronal/sagittal flip (mirrors lumen_extract_slice).
void lumen_slice_pixel_to_voxel(const LumenVolume* v, int axis, int index, int px,
                                int py, int* x, int* y, int* z);

// --- 3D surface (marching cubes) --------------------------------------------
// Generation is split so it can run off the main thread without racing the live
// mask (see the eng review's "snapshot mask, compute off-handle" decision):
//   1. lumen_mesh_snapshot  - call on the main thread; copies the current mask.
//   2. lumen_mesh_generate  - call on a background thread; marches the snapshot.
//   3. read the mesh buffers - back on the main thread, after generate returns.

// Snapshot the current mask for the next generate. Main-thread only.
void lumen_mesh_snapshot(LumenVolume* v);

// March the snapshotted mask into a surface. `smooth_iters` >= 0 softens voxel
// steps; `downsample` >= 1 coarsens the grid to cap triangles. Returns the
// triangle count. Safe to run on a background thread (touches only the snapshot
// and the mesh, never the live mask).
int lumen_mesh_generate(LumenVolume* v, int smooth_iters, int downsample);

// Mesh buffer access (valid until the next generate). Vertices/normals are 3
// floats each; indices are 3 per triangle. Pointers are NULL when empty.
int lumen_mesh_vertex_count(const LumenVolume* v);
int lumen_mesh_index_count(const LumenVolume* v);
const float* lumen_mesh_vertices(const LumenVolume* v);
const float* lumen_mesh_normals(const LumenVolume* v);
const unsigned int* lumen_mesh_indices(const LumenVolume* v);

// Write the current mesh to `path` as binary STL. Returns 0 on success, else a
// non-zero errno-style code.
int lumen_mesh_write_stl(const LumenVolume* v, const char* path);

#ifdef __cplusplus
}
#endif

#endif // LUMEN_BRIDGE_H
