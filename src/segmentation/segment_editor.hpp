// LumenSlice - the segmentation domain facade.
//
// WHAT THIS IS (for newcomers)
// ----------------------------
// `SegmentEditor` is the single object that "owns the segmentation of one volume".
// It bundles together the three things that make up a segmentation:
//
//   1. LabelVolume mask_     - one byte per voxel (0 = background, 1..255 = a
//                              segment id). This is the actual painted data.
//   2. SegmentTable segments_- the per-id metadata: colour, visibility, and which
//                              id is "active" (the target of edits).
//   3. UndoStack undo_       - the bounded edit history.
//
// It also holds a NON-owning pointer to the loaded scan (`volume_`) so editing
// methods can read HU values without the caller passing the volume in every time.
//
// Every editing operation runs through `apply()`, which takes a SegmentationEffect
// (see effects.hpp) - the Strategy pattern. The convenience methods below
// (threshold, paint, grow, ...) just build the right effect and hand it to apply().
//
// WHY IT EXISTS (the design principles)
// ------------------------------------
//   - Facade: one cohesive API over three collaborating objects + the effects.
//   - Single Responsibility: this class is "edit the segmentation of one volume".
//   - Encapsulation: the mask/table/undo are private; an invariant like "removing a
//     segment clears its voxels" can never be forgotten by a caller.
//   - Dependency direction: the C bridge depends on this class; this class depends
//     only on the pure-compute core. No UI or marshalling concerns leak in.

#pragma once

#include <cstdint>

#include "core/volume.h"
#include "geometry/plane_map.hpp" // Axis
#include "segmentation/effects.hpp"
#include "segmentation/label_volume.hpp"
#include "segmentation/segment_table.hpp"
#include "segmentation/undo_stack.hpp"

namespace lumen {

class SegmentEditor {
public:
    // Bind to a freshly loaded volume: remember it (not owned), size the mask to
    // it, forget all history, and start with a single active segment.
    void reset_to(const Volume& volume, Rgb default_color);

    // --- Segments -----------------------------------------------------------
    // Create a segment with the lowest free id (1..255); it becomes active.
    // Returns the new id, or 0 if all ids are in use.
    std::uint8_t add_segment(Rgb color);

    // Forget segment `id` AND clear its voxels from the mask (an invariant kept
    // here, not in the bridge). No-op if the id is absent.
    void remove_segment(std::uint8_t id);

    [[nodiscard]] std::uint8_t active() const { return segments_.active(); }
    void set_active(std::uint8_t id) { segments_.set_active(id); }
    [[nodiscard]] int segment_count() const { return segments_.count(); }
    [[nodiscard]] std::uint8_t segment_id_at(int index) const {
        return segments_.id_at(index);
    }

    void set_color(std::uint8_t id, Rgb color) { segments_.set_color(id, color); }
    [[nodiscard]] Rgb color(std::uint8_t id) const { return segments_.color(id); }
    void set_visible(std::uint8_t id, bool visible) {
        segments_.set_visible(id, visible);
    }
    [[nodiscard]] bool visible(std::uint8_t id) const {
        return segments_.visible(id);
    }

    // Voxels labelled with `id`, and the total across all segments.
    [[nodiscard]] long label_count(std::uint8_t id) const;
    [[nodiscard]] long total_labelled() const { return mask_.count_nonzero(); }

    // --- Editing (Strategy dispatch; all target the active segment) ----------
    // Run any effect against the active label. Returns voxels changed.
    long apply(const SegmentationEffect& effect);

    // Convenience builders for the concrete effects (what the bridge calls).
    void threshold(float low_hu, float high_hu);
    long region_grow(int x, int y, int z, float tolerance);
    long paint(Axis axis, int slice_index, int cx, int cy, int radius, bool add);
    void clear_active();
    long keep_largest();
    long remove_small(long min_voxels);
    long grow_margin(int iterations);
    long shrink_margin(int iterations);
    long smooth(int iterations);

    // --- Undo / redo --------------------------------------------------------
    // Capture the mask state BEFORE a user operation (the caller picks the
    // boundaries so a whole drag or a slider session is one undo step).
    void push_undo() { undo_.capture(mask_); }
    bool undo() { return undo_.undo(mask_); }
    bool redo() { return undo_.redo(mask_); }
    [[nodiscard]] bool can_undo() const { return undo_.can_undo(); }
    [[nodiscard]] bool can_redo() const { return undo_.can_redo(); }

    // --- Read-only views for rendering / meshing ----------------------------
    [[nodiscard]] const LabelVolume& mask() const { return mask_; }
    [[nodiscard]] const SegmentTable& segments() const { return segments_; }

private:
    const Volume* volume_ = nullptr; // the loaded scan (not owned)
    LabelVolume mask_;
    SegmentTable segments_;
    UndoStack undo_;
};

} // namespace lumen
