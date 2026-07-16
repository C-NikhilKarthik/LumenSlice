// LumenSlice C bridge - 3D surface (marching cubes) surface.
//
// One of three bridge translation units. Generation is split into snapshot (main
// thread) + generate (background thread) so the marching cubes never races the
// live mask the user keeps editing - see the comments on the three steps below and
// in lumen_bridge.h. This file reads the editor's mask through its const view; it
// never mutates the segmentation.

#include "lumen_bridge.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>

#include "lumen_handle.hpp"
#include "segmentation/label_volume.hpp"
#include "segmentation/marching_cubes.hpp"
#include "segmentation/stl_export.hpp"

namespace {

// Crop the mask into the snapshot buffer, keeping only voxels for which keep(id) is
// true, binarized to 0/1. The snapshot covers the labelled region's bounding box
// (plus a one-voxel zero margin so marching cubes closes the surface at the box
// edges) rather than the whole volume — this is what makes 3D generation scale with
// what was segmented, not with the loaded scan size. Empties the snapshot when
// nothing matches. The origin within the full volume is recorded so the generated
// vertices can be shifted back into volume space.
template <class KeepFn>
void crop_snapshot(LumenVolume* v, KeepFn keep) {
    const lumen::LabelVolume& mask = v->editor.mask();
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
    if (x1 < 0) { // nothing labelled — empty surface
        v->mesh_snapshot.clear();
        v->snap_w = v->snap_h = v->snap_d = 0;
        v->snap_ox = v->snap_oy = v->snap_oz = 0;
        return;
    }

    // One-voxel margin (clamped to the volume) so the field has a zero border.
    x0 = std::max(0, x0 - 1);
    y0 = std::max(0, y0 - 1);
    z0 = std::max(0, z0 - 1);
    x1 = std::min(W - 1, x1 + 1);
    y1 = std::min(H - 1, y1 + 1);
    z1 = std::min(D - 1, z1 + 1);

    const int cw = x1 - x0 + 1, ch = y1 - y0 + 1, cd = z1 - z0 + 1;
    v->snap_w = cw;
    v->snap_h = ch;
    v->snap_d = cd;
    v->snap_ox = x0;
    v->snap_oy = y0;
    v->snap_oz = z0;
    v->mesh_snapshot.resize(static_cast<std::size_t>(cw) * ch * cd);

    std::size_t o = 0;
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y) {
            const std::uint8_t* row =
                src + (static_cast<std::size_t>(z) * H + y) * W;
            for (int x = x0; x <= x1; ++x)
                v->mesh_snapshot[o++] = keep(row[x]) ? 1 : 0;
        }
}

} // namespace

extern "C" {

void lumen_mesh_snapshot(LumenVolume* v) {
    if (v == nullptr || !v->editor.mask().valid()) return;
    crop_snapshot(v, [](std::uint8_t id) { return id != 0; }); // every label
}

void lumen_mesh_snapshot_label(LumenVolume* v, int id) {
    if (v == nullptr || !v->editor.mask().valid() || id <= 0 || id > 255) return;
    const std::uint8_t label = static_cast<std::uint8_t>(id);
    crop_snapshot(v, [label](std::uint8_t x) { return x == label; });
}

void lumen_mesh_snapshot_labels(LumenVolume* v, const int* ids, int count) {
    if (v == nullptr || !v->editor.mask().valid() || ids == nullptr || count <= 0)
        return;
    bool keep[256] = {false};
    for (int i = 0; i < count; ++i) {
        const int id = ids[i];
        if (id > 0 && id < 256) keep[id] = true;
    }
    // Fuse the selected segments into one snapshot (their union), so the export is a
    // single surface over exactly the chosen segments.
    crop_snapshot(v, [&keep](std::uint8_t x) { return x != 0 && keep[x]; });
}

int lumen_mesh_generate(LumenVolume* v, int smooth_iters, int downsample) {
    if (v == nullptr || v->mesh_snapshot.empty()) return 0;
    const int tris = lumen::marching_cubes(
        v->mesh_snapshot.data(), v->snap_w, v->snap_h, v->snap_d,
        v->volume.spacing_x, v->volume.spacing_y, v->volume.spacing_z,
        smooth_iters, downsample, v->mesh);
    // The mesh was built in cropped-box local coordinates; translate every vertex
    // back into full-volume space by the crop origin (offset is independent of the
    // downsample factor, since it is a whole-voxel shift).
    if (tris > 0 && (v->snap_ox != 0 || v->snap_oy != 0 || v->snap_oz != 0)) {
        const float ox = static_cast<float>(v->snap_ox) * v->volume.spacing_x;
        const float oy = static_cast<float>(v->snap_oy) * v->volume.spacing_y;
        const float oz = static_cast<float>(v->snap_oz) * v->volume.spacing_z;
        auto& vert = v->mesh.vertices;
        for (std::size_t i = 0; i + 2 < vert.size(); i += 3) {
            vert[i] += ox;
            vert[i + 1] += oy;
            vert[i + 2] += oz;
        }
    }
    return tris;
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
