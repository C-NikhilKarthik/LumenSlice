// LumenSlice — bounded undo/redo for the label mask.
//
// A label mask is mostly background, so each snapshot is run-length encoded before
// it is pushed: a 512x512x300 mask (78 MB raw) compresses to a few KB when only a
// structure or two is labelled. We keep at most `kDepth` snapshots on each side,
// dropping the oldest, so the editor's memory stays bounded no matter how long the
// session runs (the eng review's "diff-based + capped snapshots" decision — RLE is
// the diff against an all-background baseline, which is what mask history looks
// like in practice).
//
// Capture model (the Swift side picks the boundaries): call capture() at the start
// of each user operation (a paint stroke, a region-grow click, a threshold commit,
// an islands cleanup). undo() restores the most recent captured state and moves the
// current state onto the redo side; redo() reverses that.

#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "segmentation/label_volume.hpp"

namespace lumen {

class UndoStack {
public:
    static constexpr std::size_t kDepth = 10;

    // Snapshot the current mask onto the undo side and invalidate redo. Call this
    // immediately BEFORE mutating the mask.
    void capture(const LabelVolume& mask);

    // Restore the previous snapshot into `mask`. Returns false if there is none.
    bool undo(LabelVolume& mask);

    // Re-apply the snapshot undone last. Returns false if there is none.
    bool redo(LabelVolume& mask);

    [[nodiscard]] bool can_undo() const { return !past_.empty(); }
    [[nodiscard]] bool can_redo() const { return !future_.empty(); }

    // Forget all history (call on a new volume load).
    void reset();

private:
    static std::vector<std::uint8_t> encode(const LabelVolume& mask);
    static void decode(const std::vector<std::uint8_t>& rle, LabelVolume& mask);

    std::deque<std::vector<std::uint8_t>> past_;
    std::deque<std::vector<std::uint8_t>> future_;
};

} // namespace lumen
