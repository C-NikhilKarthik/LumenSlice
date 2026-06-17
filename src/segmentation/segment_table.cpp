#include "segmentation/segment_table.hpp"

#include <algorithm>

namespace lumen {

void SegmentTable::rebuild_luts() {
    color_lut_.fill(Rgb{});
    visible_lut_.fill(0);
    for (const Segment& s : segments_) {
        color_lut_[s.id] = s.color;
        visible_lut_[s.id] = s.visible ? 1 : 0;
    }
}

void SegmentTable::clear() {
    segments_.clear();
    active_ = 0;
    rebuild_luts();
}

std::uint8_t SegmentTable::add(Rgb color) {
    // Lowest free id in 1..255 keeps ids dense and reuses freed slots.
    std::array<bool, 256> used{};
    for (const Segment& s : segments_) used[s.id] = true;
    std::uint8_t id = 0;
    for (int candidate = 1; candidate <= 255; ++candidate) {
        if (!used[static_cast<std::size_t>(candidate)]) {
            id = static_cast<std::uint8_t>(candidate);
            break;
        }
    }
    if (id == 0) return 0; // table full

    segments_.push_back(Segment{id, color, true});
    active_ = id;
    rebuild_luts();
    return id;
}

void SegmentTable::remove(std::uint8_t id) {
    const auto it = std::find_if(segments_.begin(), segments_.end(),
                                 [id](const Segment& s) { return s.id == id; });
    if (it == segments_.end()) return;
    segments_.erase(it);
    if (active_ == id) {
        active_ = segments_.empty() ? 0 : segments_.front().id;
    }
    rebuild_luts();
}

bool SegmentTable::exists(std::uint8_t id) const {
    return std::any_of(segments_.begin(), segments_.end(),
                       [id](const Segment& s) { return s.id == id; });
}

void SegmentTable::set_color(std::uint8_t id, Rgb c) {
    for (Segment& s : segments_) {
        if (s.id == id) {
            s.color = c;
            color_lut_[id] = c;
            return;
        }
    }
}

void SegmentTable::set_visible(std::uint8_t id, bool v) {
    for (Segment& s : segments_) {
        if (s.id == id) {
            s.visible = v;
            visible_lut_[id] = v ? 1 : 0;
            return;
        }
    }
}

} // namespace lumen
