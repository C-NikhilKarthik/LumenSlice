# Clarifications: viewer terms + the sagittal-scroll fix

A plain-language reference for a few things that are easy to misread in the UI, plus
the write-up of the sagittal-scroll smoothness issue and how it was fixed. See also
[`features/VIEWER_NAVIGATION.md`](features/VIEWER_NAVIGATION.md),
[`features/SEGMENTATION.md`](features/SEGMENTATION.md),
[`features/THREED_AND_EXPORT.md`](features/THREED_AND_EXPORT.md), and
[`engineering/PERFORMANCE.md`](engineering/PERFORMANCE.md).

---

## Window / Level (HU) — what "level" and "window" mean

A CT scan stores each voxel in **Hounsfield Units (HU)** - a physical density scale:
air ≈ -1000, water = 0, fat ≈ -100, soft tissue ≈ +40, dense bone ≈ +400 to +1000+.
The screen only has 256 shades of gray, so we have to pick which slice of that HU
scale maps onto black→white. That mapping is the **window/level**:

- **Level** (a.k.a. *center*) = the HU value shown as **mid-gray** - i.e. overall
  brightness. Raise it and the image darkens (you're centering on denser tissue);
  lower it and it brightens.
- **Window** (a.k.a. *width*) = the **HU span mapped from black to white** - i.e.
  contrast. A narrow window = high contrast over a small HU range (everything outside
  clips to pure black or pure white); a wide window = low contrast over a big range.

The visible range is therefore:

```
black below   level - window/2
white above   level + window/2
```

**The control is a two-thumb range** - the same reusable control the Segment tab uses
for its threshold. Because a window *is* a range, the two handles are the visible
**Min** and **Max** HU; dragging them sets Level to the midpoint and Window to the
width. Below the slider a live readout shows the derived **"Level N · Window N HU"**.

**Why the range is capped.** The raw HU span of a CT is enormous (roughly
-1024 … +3071), but essentially all viewing lives in a much narrower band, so the
full range would make the handles hypersensitive and mostly wasted. The window edges
are capped to a usable band (about **-1400 … +1600 HU**, clamped to the volume and
always widened to include the current window, so a preset never parks a handle
off-track). That band still covers every preset; the Min/Max fields let you type exact
edges within it.

**Presets:** Soft tissue L40/W400 · Bone L400/W1500 · Lung L-600/W1500.

---

## Seed locality (Grow from seeds)

"Grow from seeds" partitions the volume competitively among the seeded segments
(each painted seed grows outward and the segments compete for voxels, watershed
style). **Seed locality (0-10) is a distance penalty on that growth:**

- **Higher** → growth is discouraged from wandering far from the painted seeds, so
  each segment stays **compact and close to where you seeded**. Raise it when two
  structures have similar intensities and one segment's growth "leaks" into the
  other.
- **Lower (0)** → no distance penalty; growth competes on **intensity similarity
  alone**, reaching as far as the similar-intensity region extends within the seeds'
  (expanded) bounding box. Lower it when a structure is large or elongated and you
  want growth to reach its far extents.

So it's the "stay near your seeds vs. follow the intensities anywhere" dial. It only
affects the *Grow from seeds* preview, not the click-fill (Fill/flood) tool.

---

## Smoothing & Resolution (3D tab)

These two tune the marching-cubes surface, independently:

- **Smoothing (0-5)** = how many surface-smoothing passes run *after* the surface is
  extracted. `0` = raw, blocky voxel stair-steps; higher = smoother, more organic
  surface. Very high smoothing softens fine detail and can slightly shrink thin
  features, so it's a look/detail trade-off, not a quality "level".
- **Resolution (Full / Half / Third)** = how much the mask is **downsampled before**
  marching cubes (Full = every voxel, Half = every 2nd, Third = every 3rd). Full is
  the finest and slowest with the most triangles; Half/Third are coarser but generate
  faster and produce a lighter STL. Use a lower resolution for a quick preview or a
  very large volume, and **Full for the final export.**

Rule of thumb: Resolution controls *how many triangles* (detail + speed + file size);
Smoothing controls *how rounded* those triangles look.

---

## Sagittal-view scroll smoothness — issue and solution

**Symptom.** Scrolling slices in the bottom-right (sagittal) pane felt choppier than
the axial pane.

**Root cause.** The volume is stored **X-fastest**: `index = x + y·W + z·W·H`
(`src/core/volume.h`). That memory layout makes the three planes very unequal to
read:

| Plane | Held constant | Memory access | Cost |
|-------|---------------|---------------|------|
| Axial | Z | one contiguous `W×H` block | fast (cache-friendly) |
| Coronal | Y | rows contiguous in X, striding by `W·H` | medium |
| **Sagittal** | **X** | every sample strides by `W` / `W·H` | **slowest - ~one cache miss per pixel** |

On top of that, `lumen::ExtractSlice` (`src/visualization/slice_view.cpp`) calls
`plane_to_voxel()` **per pixel**, and `VolumeModel.setSlice` used to re-extract the
slice **synchronously on every scroll step**. So a fast scroll through the sagittal
plane paid that slow extraction for every intermediate slice - hence the choppiness
next to axial.

**Fix applied.** `VolumeModel.setSlice` now updates `focus` immediately (so the
crosshair and the slider track live) but **coalesces the actual re-extraction** to
the next runloop tick via `scheduleRefresh`, dropping the intermediate slices you
scroll straight past. A fast scroll now extracts about one slice per frame instead of
every slice, so the sagittal pane feels as smooth as axial. This helps all three
panes; it just matters most on the slowest one. On the Segment tab the mask overlay
is re-extracted the same coalesced way (`SegmentationModel.scheduleOverlayRefresh`),
so the overlay doesn't reintroduce the per-slice cost there.

**Deeper solution (recommended follow-up, not yet applied).** Optimize
`lumen::ExtractSlice` itself: hoist `plane_to_voxel` out of the inner loop, compute
the base offset + per-axis strides once, and walk the buffer with pointer arithmetic.
That removes the per-pixel function-call overhead and lets the compiler vectorize,
lowering even single-slice sagittal/coronal latency at the source. It was left out of
this change because it's a C++ core change that needs pixel-for-pixel validation
against the current output.
