// LumenSlice - segmentation effects (the Strategy pattern).
//
// WHAT THIS IS (for newcomers)
// ----------------------------
// A "segmentation effect" is one editing operation - threshold, paint, grow,
// smooth, and so on. Each is a small class deriving from `SegmentationEffect` and
// overriding one method, `apply()`. Because they all share that interface, the
// `SegmentEditor` can run ANY of them through a single call without knowing which
// concrete effect it holds. That is polymorphism, and the shape is the classic
// Strategy pattern: interchangeable algorithms behind one interface.
//
// WHY (the design principles)
// ---------------------------
//   - Open/Closed: adding a new effect (e.g. a future "Scissors") means adding a
//     new subclass here, not editing the editor or the bridge. Existing code is
//     closed for modification, open for extension.
//   - Single Responsibility: each effect class does exactly one operation, and its
//     parameters live as its own fields (a ThresholdEffect carries its lo/hi).
//   - Encapsulation: the parameters are private; an effect is constructed once with
//     its settings and then applied.
//
// The heavy numeric work still lives as pure free functions in segment.hpp and
// analysis.hpp (they are stateless math, trivially unit-testable in isolation).
// The effect classes are the object-oriented surface over those kernels: they hold
// the parameters and present the uniform, extensible `apply()` contract.

#pragma once

#include <cstdint>

#include "core/volume.h"
#include "geometry/plane_map.hpp" // Axis
#include "segmentation/label_volume.hpp"

namespace lumen {

// Abstract base: one interchangeable editing operation on the active label.
class SegmentationEffect {
public:
    virtual ~SegmentationEffect() = default;

    // Apply the effect to `mask`, targeting `label`, reading HU from `volume`
    // (effects that work purely on the mask ignore it). Returns the number of
    // voxels changed, so callers can decide whether a redraw is needed.
    [[nodiscard]] virtual long apply(const Volume& volume, LabelVolume& mask,
                                     std::uint8_t label) const = 0;
};

// Re-fill the active segment from a HU window (background + own voxels only).
class ThresholdEffect final : public SegmentationEffect {
public:
    ThresholdEffect(float low_hu, float high_hu) : low_(low_hu), high_(high_hu) {}
    [[nodiscard]] long apply(const Volume& volume, LabelVolume& mask,
                             std::uint8_t label) const override;

private:
    float low_;
    float high_;
};

// 6-connected flood fill from a seed voxel within `tolerance` HU.
class RegionGrowEffect final : public SegmentationEffect {
public:
    RegionGrowEffect(int seed_x, int seed_y, int seed_z, float tolerance)
        : x_(seed_x), y_(seed_y), z_(seed_z), tolerance_(tolerance) {}
    [[nodiscard]] long apply(const Volume& volume, LabelVolume& mask,
                             std::uint8_t label) const override;

private:
    int x_, y_, z_;
    float tolerance_;
};

// Paint (add) or erase a filled disk on one slice plane.
class PaintEffect final : public SegmentationEffect {
public:
    PaintEffect(Axis axis, int slice_index, int center_x, int center_y, int radius,
                bool add)
        : axis_(axis), index_(slice_index), cx_(center_x), cy_(center_y),
          radius_(radius), add_(add) {}
    [[nodiscard]] long apply(const Volume& volume, LabelVolume& mask,
                             std::uint8_t label) const override;

private:
    Axis axis_;
    int index_, cx_, cy_, radius_;
    bool add_;
};

// Level tracing: flood one slice's iso-level region (>= the clicked HU) into the
// active label. Holds the clicked slice + pixel, like PaintEffect.
class LevelTraceEffect final : public SegmentationEffect {
public:
    LevelTraceEffect(Axis axis, int slice_index, int px, int py)
        : axis_(axis), index_(slice_index), px_(px), py_(py) {}
    [[nodiscard]] long apply(const Volume& volume, LabelVolume& mask,
                             std::uint8_t label) const override;

private:
    Axis axis_;
    int index_, px_, py_;
};

// Clear every voxel of the active label back to background.
class ClearEffect final : public SegmentationEffect {
public:
    [[nodiscard]] long apply(const Volume& volume, LabelVolume& mask,
                             std::uint8_t label) const override;
};

// Keep only the largest connected component of the active label.
class KeepLargestEffect final : public SegmentationEffect {
public:
    [[nodiscard]] long apply(const Volume& volume, LabelVolume& mask,
                             std::uint8_t label) const override;
};

// Remove every connected component smaller than `min_voxels`.
class RemoveSmallEffect final : public SegmentationEffect {
public:
    explicit RemoveSmallEffect(long min_voxels) : min_voxels_(min_voxels) {}
    [[nodiscard]] long apply(const Volume& volume, LabelVolume& mask,
                             std::uint8_t label) const override;

private:
    long min_voxels_;
};

// Grow (dilate) the active label outward by `iterations` voxel layers.
class GrowMarginEffect final : public SegmentationEffect {
public:
    explicit GrowMarginEffect(int iterations) : iterations_(iterations) {}
    [[nodiscard]] long apply(const Volume& volume, LabelVolume& mask,
                             std::uint8_t label) const override;

private:
    int iterations_;
};

// Shrink (erode) the active label inward by `iterations` voxel layers.
class ShrinkMarginEffect final : public SegmentationEffect {
public:
    explicit ShrinkMarginEffect(int iterations) : iterations_(iterations) {}
    [[nodiscard]] long apply(const Volume& volume, LabelVolume& mask,
                             std::uint8_t label) const override;

private:
    int iterations_;
};

// Smooth the active label with a 26-neighbour majority filter.
class SmoothEffect final : public SegmentationEffect {
public:
    explicit SmoothEffect(int iterations) : iterations_(iterations) {}
    [[nodiscard]] long apply(const Volume& volume, LabelVolume& mask,
                             std::uint8_t label) const override;

private:
    int iterations_;
};

} // namespace lumen
