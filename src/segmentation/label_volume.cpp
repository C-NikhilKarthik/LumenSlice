#include "segmentation/label_volume.hpp"

#include <algorithm>

namespace lumen {

void LabelVolume::reset_to(const Volume& vol) {
    width_ = vol.width;
    height_ = vol.height;
    depth_ = vol.depth;
    labels_.assign(voxel_count(), 0);
}

void LabelVolume::clear() {
    std::fill(labels_.begin(), labels_.end(), static_cast<std::uint8_t>(0));
}

long LabelVolume::count_nonzero() const {
    long n = 0;
    for (std::uint8_t v : labels_) {
        if (v != 0) ++n;
    }
    return n;
}

} // namespace lumen
