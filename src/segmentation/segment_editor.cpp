#include "segmentation/segment_editor.hpp"

#include <cstddef>

#include "segmentation/grow_from_seeds.hpp"
#include "segmentation/scissor.hpp"

namespace lumen {

void SegmentEditor::reset_to(const Volume& volume, Rgb default_color) {
    volume_ = &volume;            // remembered, not owned
    mask_.reset_to(volume);
    segments_.clear();
    segments_.add(default_color); // one active segment to start
    undo_.reset();
}

std::uint8_t SegmentEditor::add_segment(Rgb color) {
    return segments_.add(color);
}

void SegmentEditor::remove_segment(std::uint8_t id) {
    if (id == 0 || !segments_.exists(id)) return;
    // Invariant: a removed segment leaves no orphan voxels behind.
    std::uint8_t* data = mask_.data();
    const std::size_t n = mask_.voxel_count();
    for (std::size_t i = 0; i < n; ++i) {
        if (data[i] == id) data[i] = 0;
    }
    segments_.remove(id);
}

long SegmentEditor::label_count(std::uint8_t id) const {
    if (id == 0) return 0;
    const std::uint8_t* data = mask_.data();
    const std::size_t n = mask_.voxel_count();
    long count = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (data[i] == id) ++count;
    }
    return count;
}

long SegmentEditor::apply(const SegmentationEffect& effect) {
    if (volume_ == nullptr) return 0;
    return effect.apply(*volume_, mask_, segments_.active());
}

// Each convenience method builds the matching effect and dispatches it. The
// editor never branches on "which operation" - that is the effect's identity.
void SegmentEditor::threshold(float low_hu, float high_hu) {
    apply(ThresholdEffect{low_hu, high_hu});
}

long SegmentEditor::region_grow(int x, int y, int z, float tolerance) {
    return apply(RegionGrowEffect{x, y, z, tolerance});
}

long SegmentEditor::paint(Axis axis, int slice_index, int cx, int cy, int radius,
                          bool add) {
    return apply(PaintEffect{axis, slice_index, cx, cy, radius, add});
}

void SegmentEditor::clear_active() { apply(ClearEffect{}); }

long SegmentEditor::keep_largest() { return apply(KeepLargestEffect{}); }

long SegmentEditor::remove_small(long min_voxels) {
    return apply(RemoveSmallEffect{min_voxels});
}

long SegmentEditor::grow_margin(int iterations) {
    return apply(GrowMarginEffect{iterations});
}

long SegmentEditor::shrink_margin(int iterations) {
    return apply(ShrinkMarginEffect{iterations});
}

long SegmentEditor::smooth(int iterations) {
    return apply(SmoothEffect{iterations});
}

long SegmentEditor::grow_from_seeds(int max_iters, int margin) {
    if (volume_ == nullptr) return 0;
    return lumen::grow_from_seeds(*volume_, mask_, max_iters, margin);
}

long SegmentEditor::scissor_cut(const float* mvp, int vp_w, int vp_h,
                                const float* poly_xy, int poly_count,
                                bool erase_inside, std::uint8_t only_label) {
    if (volume_ == nullptr) return 0;
    return lumen::scissor_cut(*volume_, mask_, mvp, vp_w, vp_h, poly_xy, poly_count,
                              erase_inside, only_label);
}

} // namespace lumen
