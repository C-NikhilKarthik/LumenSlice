// LumenSlice - the bridge's private handle definition.
//
// This header is intentionally NOT under bridge/include (the public C surface);
// it is shared only between the bridge's own translation units (lumen_bridge_*.cpp).
// `LumenVolume` is the concrete type behind the opaque `LumenVolume*` that Swift
// holds: a loaded scan plus everything derived from it (reusable extraction
// buffers, the segmentation editor, the 3D mesh + its snapshot). Splitting the
// bridge into volume/segment/mesh translation units keeps each file to a single
// responsibility; they all need this struct, so it lives here.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/volume.h"
#include "segmentation/marching_cubes.hpp"
#include "segmentation/segment_editor.hpp"
#include "visualization/slice_view.h"
#include "visualization/volume_texture.h"

struct LumenVolume {
    lumen::Volume volume;
    lumen::SliceImage scratch;      // reused grayscale slice buffer
    lumen::SliceImage mask_scratch; // reused overlay buffer
    lumen::VolumeTexture volume_texture_scratch; // reused GPU upload texture
    bool volume_texture_cache_valid = false;
    float volume_texture_cache_level = 0.0f;
    float volume_texture_cache_window = 0.0f;
    int volume_texture_cache_limit = 0;
    // The grayscale slice is immutable until one of these request parameters
    // changes. Keeping this cache at the bridge seam benefits both Swift and Qt,
    // especially when a repaint only changes crosshair/brush/UI chrome.
    bool slice_cache_valid = false;
    int slice_cache_axis = 0;
    int slice_cache_index = 0;
    float slice_cache_level = 0.0f;
    float slice_cache_window = 0.0f;
    // The overlay is derived from the editor mask and segment presentation
    // state. A revision lets both UI frontends reuse it across repaint events.
    std::uint64_t mask_revision = 1;
    bool mask_cache_valid = false;
    std::uint64_t mask_cache_revision = 0;
    int mask_cache_axis = 0;
    int mask_cache_index = 0;
    lumen::SegmentEditor editor;    // owns mask + segments + undo
    std::string meta_json;

    lumen::Mesh mesh;
    std::vector<std::uint8_t> mesh_snapshot; // frozen mask, CROPPED to the labelled
                                             // region's bounding box (not the whole
                                             // volume) so marching cubes scales with
                                             // what was segmented, not the scan size.
    int snap_w = 0;
    int snap_h = 0;
    int snap_d = 0;
    // Origin of the cropped snapshot within the full volume (voxels). Mesh vertices
    // come out in cropped-local mm and are shifted back by origin * spacing.
    int snap_ox = 0;
    int snap_oy = 0;
    int snap_oz = 0;
};

namespace lumen_bridge_detail {

// Clamp an int colour/id component into the 0..255 byte range.
inline std::uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<std::uint8_t>(v);
}

inline void invalidate_mask_cache(LumenVolume* v) {
    if (v == nullptr) return;
    ++v->mask_revision;
    v->mask_cache_valid = false;
}

} // namespace lumen_bridge_detail
