// LumenSlice C bridge - 3D surface (marching cubes) surface.
//
// One of three bridge translation units. Generation is split into snapshot (main
// thread) + generate (background thread) so the marching cubes never races the
// live mask the user keeps editing - see the comments on the three steps below and
// in lumen_bridge.h. This file reads the editor's mask through its const view; it
// never mutates the segmentation.

#include "lumen_bridge.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>

#include "lumen_handle.hpp"
#include "segmentation/marching_cubes.hpp"
#include "segmentation/stl_export.hpp"

extern "C" {

void lumen_mesh_snapshot(LumenVolume* v) {
    if (v == nullptr || !v->editor.mask().valid()) return;
    const lumen::LabelVolume& mask = v->editor.mask();
    v->snap_w = mask.width();
    v->snap_h = mask.height();
    v->snap_d = mask.depth();
    v->mesh_snapshot.assign(mask.data(), mask.data() + mask.voxel_count());
}

void lumen_mesh_snapshot_label(LumenVolume* v, int id) {
    if (v == nullptr || !v->editor.mask().valid() || id <= 0 || id > 255) return;
    const lumen::LabelVolume& mask = v->editor.mask();
    const std::uint8_t label = static_cast<std::uint8_t>(id);
    v->snap_w = mask.width();
    v->snap_h = mask.height();
    v->snap_d = mask.depth();
    const std::size_t n = mask.voxel_count();
    const std::uint8_t* src = mask.data();
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
