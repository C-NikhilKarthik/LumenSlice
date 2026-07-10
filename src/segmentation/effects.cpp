#include "segmentation/effects.hpp"

#include <cstddef>

#include "segmentation/analysis.hpp"
#include "segmentation/segment.hpp"

// Each effect overrides apply() and delegates to the matching pure-compute kernel
// in segment.hpp / analysis.hpp. Effects that work purely on the mask ignore the
// `volume` parameter (its name is omitted to say so explicitly).

namespace lumen {

long ThresholdEffect::apply(const Volume& volume, LabelVolume& mask,
                            std::uint8_t label) const {
    threshold_fill(volume, low_, high_, mask, label);
    // A full re-fill always "changes" the segment as far as the UI is concerned.
    return mask.count_nonzero();
}

long RegionGrowEffect::apply(const Volume& volume, LabelVolume& mask,
                             std::uint8_t label) const {
    return region_grow(volume, x_, y_, z_, tolerance_, mask, label);
}

long PaintEffect::apply(const Volume& volume, LabelVolume& mask,
                        std::uint8_t label) const {
    return paint_disk(volume, axis_, index_, cx_, cy_, radius_, add_, mask, label);
}

long ClearEffect::apply(const Volume& /*volume*/, LabelVolume& mask,
                        std::uint8_t label) const {
    if (label == 0) return 0;
    std::uint8_t* data = mask.data();
    const std::size_t n = mask.voxel_count();
    long cleared = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (data[i] == label) {
            data[i] = 0;
            ++cleared;
        }
    }
    return cleared;
}

long KeepLargestEffect::apply(const Volume& /*volume*/, LabelVolume& mask,
                              std::uint8_t label) const {
    return keep_largest_island(mask, label);
}

long RemoveSmallEffect::apply(const Volume& /*volume*/, LabelVolume& mask,
                              std::uint8_t label) const {
    return remove_small_islands(mask, label, min_voxels_);
}

long GrowMarginEffect::apply(const Volume& /*volume*/, LabelVolume& mask,
                             std::uint8_t label) const {
    return dilate_label(mask, label, iterations_);
}

long ShrinkMarginEffect::apply(const Volume& /*volume*/, LabelVolume& mask,
                               std::uint8_t label) const {
    return erode_label(mask, label, iterations_);
}

long SmoothEffect::apply(const Volume& /*volume*/, LabelVolume& mask,
                         std::uint8_t label) const {
    return smooth_label(mask, label, iterations_);
}

long LevelTraceEffect::apply(const Volume& volume, LabelVolume& mask,
                             std::uint8_t label) const {
    return level_trace(volume, axis_, index_, px_, py_, mask, label);
}

} // namespace lumen
