// LumenSlice — the segment registry (multi-segment metadata).
//
// The LabelVolume stores one byte per voxel: 0 = background, 1..255 = a segment
// id. This table is the side-car that gives each live id a colour and a
// visibility flag, plus tracks which id is "active" (the target of edits). Names
// live on the Swift side (UI-only, keyed by id); the C++ core only needs colour
// and visibility, which it uses to render the overlay and to drive per-segment
// 3D surfaces.
//
// Two parallel representations are kept deliberately:
//   - `segments_`  : the ordered list the UI iterates (stable add order).
//   - color/visible LUTs indexed by id : O(1) per-voxel lookup for ExtractMaskSlice
//     and marching cubes, which touch every voxel and cannot afford a search.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace lumen {

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

struct Segment {
    std::uint8_t id = 0;
    Rgb color;
    bool visible = true;
};

class SegmentTable {
public:
    SegmentTable() { rebuild_luts(); }

    // Remove every segment (back to "no segments"). The caller is responsible for
    // clearing the mask voxels — this only forgets the metadata.
    void clear();

    // Create a new segment with the lowest free id in 1..255 and the given colour.
    // Returns the new id, or 0 if all 255 ids are in use. The new segment becomes
    // active.
    std::uint8_t add(Rgb color);

    // Forget segment `id` (no-op if absent). If it was active, the active id falls
    // back to the first remaining segment (or 0 if none remain).
    void remove(std::uint8_t id);

    [[nodiscard]] bool exists(std::uint8_t id) const;
    [[nodiscard]] int count() const { return static_cast<int>(segments_.size()); }
    [[nodiscard]] const std::vector<Segment>& segments() const { return segments_; }

    // Ordered access for the bridge's index-based enumeration.
    [[nodiscard]] std::uint8_t id_at(int index) const {
        if (index < 0 || index >= count()) return 0;
        return segments_[static_cast<std::size_t>(index)].id;
    }

    void set_color(std::uint8_t id, Rgb c);
    void set_visible(std::uint8_t id, bool v);

    [[nodiscard]] Rgb color(std::uint8_t id) const { return color_lut_[id]; }
    [[nodiscard]] bool visible(std::uint8_t id) const { return visible_lut_[id] != 0; }

    [[nodiscard]] std::uint8_t active() const { return active_; }
    void set_active(std::uint8_t id) {
        if (id == 0 || exists(id)) active_ = id;
    }

    // Raw LUT access for the hot per-voxel loops (256 entries, indexed by id).
    [[nodiscard]] const Rgb* color_table() const { return color_lut_.data(); }
    [[nodiscard]] const unsigned char* visible_table() const {
        return visible_lut_.data();
    }

private:
    void rebuild_luts();

    std::vector<Segment> segments_;
    std::array<Rgb, 256> color_lut_{};
    std::array<unsigned char, 256> visible_lut_{};
    std::uint8_t active_ = 0;
};

} // namespace lumen
