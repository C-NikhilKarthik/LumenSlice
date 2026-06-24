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

struct LumenVolume {
    lumen::Volume volume;
    lumen::SliceImage scratch;      // reused grayscale slice buffer
    lumen::SliceImage mask_scratch; // reused overlay buffer
    lumen::SegmentEditor editor;    // owns mask + segments + undo
    std::string meta_json;

    lumen::Mesh mesh;
    std::vector<std::uint8_t> mesh_snapshot; // frozen mask the mesh is built from
    int snap_w = 0;
    int snap_h = 0;
    int snap_d = 0;
};

namespace lumen_bridge_detail {

// Clamp an int colour/id component into the 0..255 byte range.
inline std::uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<std::uint8_t>(v);
}

} // namespace lumen_bridge_detail
