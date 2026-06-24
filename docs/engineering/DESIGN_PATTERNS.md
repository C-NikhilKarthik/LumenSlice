# Design patterns in LumenSlice

This codebase uses a handful of well-known design patterns. Each one is here for a
concrete reason, not for show. This doc names each pattern, says why it earns its
place, and points at the exact file so you can read the real thing. The last section
is a step-by-step recipe for the most common extension: adding a new segmentation
tool.

A quick map first:

| Pattern | Where | What it buys us |
|---|---|---|
| Facade | `SegmentEditor`, the C bridge | One simple API over several collaborators |
| Strategy | `SegmentationEffect` + concrete effects | Interchangeable, extensible operations |
| Command / Memento | `UndoStack` | Bounded undo/redo via state snapshots |
| Adapter | `src/bridge/` | Let Swift call C++ across a C seam |
| Registry + lookup table | `SegmentTable` | Manage segments; O(1) per-voxel colour lookup |
| Single source of truth | `plane_map` | One place for slice <-> voxel geometry |
| Observer / MVVM | Swift `ObservableObject` models | UI reacts to model changes |
| Snapshot-then-compute | mesh generation | Background work that never races live data |

## 1. Facade - `SegmentEditor`

**What.** A facade gives one clean, high-level object in front of several smaller
ones. `SegmentEditor` (`src/segmentation/segment_editor.hpp`) sits in front of three
collaborators: the `LabelVolume` mask, the `SegmentTable` metadata, and the
`UndoStack` history, plus a non-owning pointer to the loaded `Volume`.

**Why.** Before this class, the C bridge itself contained domain rules like "removing
a segment must also clear its voxels" and "clearing only touches the active label."
That mixed two jobs: marshalling languages and editing masks. The facade pulls all
the editing rules into one place, so:

- the bridge becomes a thin translator (Single Responsibility),
- invariants are enforced in one spot and cannot be forgotten by a caller
  (encapsulation - the mask/table/undo are `private`),
- the rest of the app talks to one object instead of three.

The C bridge (`src/bridge/`) is itself a facade-plus-adapter: one flat C surface over
the whole core. Read `lumen_bridge.h` and you have the entire app-to-core contract.

## 2. Strategy - `SegmentationEffect` and its subclasses

**What.** The Strategy pattern makes a family of operations interchangeable behind one
interface. `SegmentationEffect` (`src/segmentation/effects.hpp`) is an abstract base
class with a single method:

```cpp
class SegmentationEffect {
public:
    virtual ~SegmentationEffect() = default;
    [[nodiscard]] virtual long apply(const Volume& volume, LabelVolume& mask,
                                     std::uint8_t label) const = 0;
};
```

Each editing operation is a small subclass that overrides `apply()`:
`ThresholdEffect`, `PaintEffect`, `RegionGrowEffect`, `ClearEffect`,
`KeepLargestEffect`, `RemoveSmallEffect`, `GrowMarginEffect`, `ShrinkMarginEffect`,
`SmoothEffect`. Each holds its own parameters as private fields (a `ThresholdEffect`
carries its `low_`/`high_`).

**Why.** The `SegmentEditor` runs any of them through one call:

```cpp
long SegmentEditor::apply(const SegmentationEffect& effect) {
    return effect.apply(*volume_, mask_, segments_.active());
}
```

The editor never asks "which operation is this?" - it just calls `apply()` and the
right subclass runs. That is **polymorphism**, and it gives us the **Open/Closed
Principle**: a new tool is a new subclass; the editor and bridge do not change.

The actual number-crunching still lives as pure free functions (`threshold_fill`,
`region_grow`, `dilate_label`, ...) in `segment.cpp` and `analysis.cpp`. Those are
stateless math, easy to unit-test in isolation. The effect classes are the
object-oriented surface over them: they hold the parameters and present the uniform
`apply()` contract. Both styles working together is intentional, not an accident.

## 3. Command / Memento - `UndoStack`

**What.** A *memento* is a captured snapshot of an object's state you can restore
later. `UndoStack` (`src/segmentation/undo_stack.hpp`) keeps two stacks of mask
snapshots (past and future). `capture()` saves the current mask before an edit;
`undo()` restores the previous snapshot; `redo()` replays it.

**Why.** Snapshots are simple and correct. The twist is memory: a full mask can be
tens of megabytes, so each snapshot is **run-length encoded** before it is stored (a
mostly-empty mask compresses to a few kilobytes), and the history is capped at a
fixed depth. That keeps undo bounded no matter how long the session runs.

The Swift side decides *when* to capture (start of a paint stroke, a threshold
session, an islands cleanup), so a whole drag collapses into one undo step. See the
capture points in `app/Model/SegmentationModel.swift`.

## 4. Adapter - the C bridge

**What.** An adapter makes one interface usable through another. Swift speaks C, the
core speaks C++. `src/bridge/` adapts between them: plain C functions in, C++ method
calls out. The opaque `LumenVolume*` handle hides the C++ struct entirely.

**Why.** It is the only way Swift can drive C++ safely, and keeping it thin means the
two languages stay decoupled. The bridge is split by concern into three files
(`lumen_bridge_volume.cpp`, `lumen_bridge_segment.cpp`, `lumen_bridge_mesh.cpp`) that
share the private handle definition in `lumen_handle.hpp`.

## 5. Registry + lookup table - `SegmentTable`

**What.** `SegmentTable` (`src/segmentation/segment_table.hpp`) is the registry of
live segments: it allocates ids (1..255), tracks each segment's colour and
visibility, and remembers which id is "active."

**Why the two representations.** It keeps both an ordered list (for the UI to iterate)
and a 256-entry colour/visibility lookup table indexed by id. The overlay renderer
(`mask_view.cpp`) and marching cubes touch every voxel, so they cannot afford to
search a list per voxel - they index the table in O(1). Maintaining both is a
deliberate space-for-speed trade.

## 6. Single source of truth - `plane_map`

**What.** `plane_map` (`src/geometry/plane_map.hpp`) is the only place that converts
between a 2D slice pixel and a 3D voxel, on every axis, including the coronal/sagittal
vertical flip.

**Why.** Slice extraction, the crosshair, click-to-locate, and paint all need the same
mapping. If each had its own copy, one would eventually disagree and paint or seed the
wrong voxel silently. One function, used everywhere, makes that class of bug
impossible. There is a round-trip unit test guarding it in `seg_test.cpp`.

## 7. Observer / MVVM - the Swift models

**What.** The Swift app is MVVM. `VolumeModel`, `SegmentationModel`, and `MeshModel`
are `ObservableObject`s; the SwiftUI views observe them and re-render when their
`@Published` values change.

**Why.** It decouples UI from logic. One subtle performance point worth knowing: the
mask overlay images live in a separate tiny observable (`OverlayStore`) so that a
paint stroke re-renders only the slice panes, not the whole control panel. That split
is what makes brushing feel fluid.

## 8. Snapshot-then-compute - mesh generation

**What.** Not a Gang-of-Four pattern, but an important concurrency idiom here. 3D mesh
generation copies ("snapshots") the mask on the main thread, then runs marching cubes
on a background thread reading only that snapshot.

**Why.** The user keeps painting while the mesh builds. Computing directly on the live
mask would be a data race. The snapshot makes the background work read-only and safe.
See `lumen_mesh_snapshot` / `lumen_mesh_generate` and `app/ThreeD/MeshModel.swift`.

---

## Recipe: adding a new segmentation tool

Say you want a new effect, "Invert" (flip labelled <-> background inside the active
segment's bounding region). Thanks to the Strategy pattern, you touch new code, not
existing logic:

1. **Add the kernel** (optional). If there is real math, add a pure function in
   `src/segmentation/analysis.cpp` (and declare it in `analysis.hpp`), and give it a
   unit test in `tests/cpp/seg_test.cpp`. Stateless math is easiest to test here.

2. **Add the effect class.** In `src/segmentation/effects.hpp`, add a subclass:

   ```cpp
   class InvertEffect final : public SegmentationEffect {
   public:
       [[nodiscard]] long apply(const Volume& volume, LabelVolume& mask,
                                std::uint8_t label) const override;
   };
   ```

   and implement `apply()` in `effects.cpp` (delegating to your kernel).

3. **Add a convenience method** on `SegmentEditor` (`segment_editor.hpp/.cpp`):

   ```cpp
   long SegmentEditor::invert() { return apply(InvertEffect{}); }
   ```

4. **Expose it through the bridge.** Declare `long lumen_seg_invert(LumenVolume*)` in
   `src/bridge/include/lumen_bridge.h` and implement the one-liner in
   `lumen_bridge_segment.cpp` (`return v->editor.invert();`).

5. **Wire the UI.** Add a `SegTool` case (or a button) in `SegmentControls.swift`, a
   model method in `SegmentationModel.swift` that calls the bridge and captures undo,
   and you are done.

Notice what you did NOT touch: `SegmentEditor::apply`, the other effects, the mesh
code, or the geometry. New behaviour arrived as new classes. That is the Open/Closed
Principle paying off.
