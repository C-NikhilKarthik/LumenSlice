// LumenSlice — the segmentation mask.
//
// A label volume is one byte per voxel, laid out identically to the HU buffer
// (X-fastest, then Y, then Z) so a voxel's mask byte is at the same linear index
// as its HU value. 0 = background; non-zero = a segment label. The MVP uses a
// single active label (kActiveLabel); the uint8 width leaves room for multi-label
// segmentation later without changing the storage.
//
// Undo/redo is added in phase P1b (diff-based, see the plan); this phase keeps the
// mask itself plus the cheap whole-mask operations that fill it.

#pragma once

#include <cstdint>
#include <vector>

#include "core/volume.h"

namespace lumen {

inline constexpr std::uint8_t kActiveLabel = 1;

class LabelVolume {
public:
    // Size the mask to match `vol` and clear it. Cheap to call on every load.
    void reset_to(const Volume& vol);

    // Clear every voxel to background without resizing.
    void clear();

    [[nodiscard]] bool valid() const {
        return width_ > 0 && height_ > 0 && depth_ > 0 &&
               labels_.size() == voxel_count();
    }

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] int depth() const { return depth_; }

    [[nodiscard]] std::size_t voxel_count() const {
        return static_cast<std::size_t>(width_) *
               static_cast<std::size_t>(height_) *
               static_cast<std::size_t>(depth_);
    }

    [[nodiscard]] bool in_bounds(int x, int y, int z) const {
        return x >= 0 && y >= 0 && z >= 0 && x < width_ && y < height_ &&
               z < depth_;
    }

    [[nodiscard]] std::size_t index(int x, int y, int z) const {
        return static_cast<std::size_t>(x) +
               static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
               static_cast<std::size_t>(z) * static_cast<std::size_t>(width_) *
                   static_cast<std::size_t>(height_);
    }

    // Bounds-checked read; returns 0 (background) when out of range.
    [[nodiscard]] std::uint8_t at(int x, int y, int z) const {
        if (!in_bounds(x, y, z)) return 0;
        return labels_[index(x, y, z)];
    }

    // Bounds-checked write; no-op when out of range.
    void set(int x, int y, int z, std::uint8_t value) {
        if (!in_bounds(x, y, z)) return;
        labels_[index(x, y, z)] = value;
    }

    // Raw access for the whole-volume operations (threshold, marching cubes).
    [[nodiscard]] const std::uint8_t* data() const { return labels_.data(); }
    [[nodiscard]] std::uint8_t* data() { return labels_.data(); }

    [[nodiscard]] long count_nonzero() const;

private:
    int width_ = 0;
    int height_ = 0;
    int depth_ = 0;
    std::vector<std::uint8_t> labels_;
};

} // namespace lumen
