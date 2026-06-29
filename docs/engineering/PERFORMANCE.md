# Performance: segmentation, 3D generation, and the build

This is the "why is it fast (and why was it slow)" companion to
[ARCHITECTURE.md](ARCHITECTURE.md). It covers the three things that dominate
interactive cost in LumenSlice — **painting a segmentation**, **generating the 3D
surface**, and the **build configuration** that multiplies both — and the fixes that
keep them snappy on real, large scans (the reference scan below is 512×512×1774 ≈
465M voxels, ~37 MB of surface).

If you only remember one thing: the heavy work is *voxel arithmetic over big flat
arrays*, so the wins all come from **touching fewer voxels** and **letting the
compiler optimize the loops**.

---

## 1. The pipeline at a glance

A segmentation lives as one byte per voxel (`LabelVolume`, same X-fastest layout as
the HU `Volume`). 0 = background, 1..255 = a segment label. Everything below reads or
writes that mask.

```
  user paints/thresholds        mask edited          UI refresh
  on a slice            ───►   (C++ LabelVolume) ──►  - overlay image for the plane
                                                      - per-segment voxel counts
                               press Generate
                                     │
                                     ▼
                          snapshot ─► marching cubes ─► SCNGeometry (3D pane)
```

Three hot paths fall out of this: the **overlay rebuild** on every paint tick, the
**voxel-count refresh** after every edit, and **marching cubes** on Generate. Each
was, at some point, doing work proportional to the *whole volume* when it only needed
to do work proportional to *what you touched*. That is the recurring theme.

---

## 2. The 3D viewer pane

The 3D surface used to live only on the dedicated *3D* / *Export* tabs. It is now also
a fourth pane in the top-right of the slice workspace quad:

```
  ┌──────────────┬──────────────┐
  │  Axial       │  3D viewer   │
  ├──────────────┼──────────────┤
  │  Coronal     │  Sagittal    │
  └──────────────┴──────────────┘
```

- **`app/Viewer/ThreeDPane.swift`** — a peer of `SlicePane` (same chrome + maximize
  button) wrapping the existing SceneKit viewport, plus an inline *Generate / Update*
  button so you can build a surface without leaving the slice view.
- **`app/Viewer/SliceBoard.swift`** — the quad layout. Its focus state is an enum
  (`.axis(Int)` or `.threeD`) so any pane, including 3D, can be maximized.
- **`app/ThreeD/MeshSceneView.swift`** — the SceneKit `SCNView`. Camera orbit / zoom /
  pan are free (`allowsCameraControl`); it reframes the camera only when the geometry
  set actually changes, so dragging never rebuilds the scene.

Nothing here is the bottleneck — building `SCNGeometry` from the vertex buffers is
cheap relative to producing them. The cost is upstream, in marching cubes.

---

## 3. Why generation was slow — and the fix

### The problem

Marching cubes scans a grid of cells and, for each, looks at its 8 corners to decide
which triangles to emit. The original `lumen_mesh_snapshot*` copied the **entire**
mask into the snapshot buffer, so marching cubes iterated **every w×h×d cell** — even
if you had painted three small disks. On the reference scan that is ~465M cells, each
doing 8 corner lookups + a table lookup, with an `unordered_map` welding vertices
across *all* of it. Worse, the map was pre-sized with `reserve(field_count)` — for the
full volume that is a multi-gigabyte hash table allocation before a single triangle is
produced.

So generation cost scaled with **how big the scan you loaded was**, not with **how
much you segmented**. That is exactly backwards for the common case (a small structure
in a big CT).

### The fix — crop to the labelled bounding box

`src/bridge/lumen_bridge_mesh.cpp` now crops the snapshot to the **bounding box of the
labelled voxels**, plus a one-voxel zero margin so the surface still closes at the box
edges:

1. One linear pass over the mask finds the inclusive bbox of voxels matching the
   segment (or any non-zero voxel, for the "all segments" STL export).
2. The snapshot is resized to just that box (`snap_w/h/d`) and the box origin within
   the full volume is recorded (`snap_ox/oy/oz`).
3. Marching cubes runs over the small box. Its vertices come out in box-local mm, so
   `lumen_mesh_generate` shifts every vertex back into volume space by
   `origin × spacing`. This offset is a whole-voxel translation, so it is exact
   regardless of the downsample factor.

Now marching cubes — and the giant `unordered_map` — see only the segmented region.
For a few painted slices the box is tiny and generation is effectively instant.

**The honest caveat:** the bbox is a *box*. If a segment is spread across the whole
volume (e.g. a full-body bone threshold), the box ≈ the whole volume and the crop
saves little. That case is still fine, but only because of the build fix in §5.

The one remaining whole-volume cost is the single linear pass to find the bbox — a
tight, memory-bandwidth-bound byte scan, far cheaper than marching over every cell.

---

## 4. Why painting was laggy — two separate causes

### 4a. N+1 full-volume scans on every edit

After every edit (including the *end of every brush stroke*) the UI calls
`reloadSegments()` to refresh each segment's voxel count and the total. The original
code did this with **one full-volume scan per segment** (`lumen_seg_label_count`) plus
**one more** for the total (`lumen_seg_count`). On a deep scan with several segments
that is several hundred-MB scans back-to-back — a visible hitch at the end of each
stroke, scaling with both volume size and segment count.

**Fix:** `lumen_seg_label_histogram` (in `src/bridge/lumen_bridge_segment.cpp`) does
**one** pass and fills a 256-entry table of per-label counts. Swift reads each
segment's count from the table and sums entries 1..255 for the total. **N+1 scans → 1.**

### 4b. Rebuilding the whole slice overlay every drag tick

The mask overlay for a plane is a premultiplied-RGBA image the size of that slice.
Every paint tick re-extracted the *entire* slice and built a fresh `CGImage`. For an
axial slice that is 512×512; but **coronal/sagittal slices are width×depth and
height×depth — they grow with the number of files loaded.** So the more you uploaded,
the heavier each paint frame, and macOS drag events can fire faster than the eye needs
(120 Hz on ProMotion).

**Fix:** `SegmentationModel.paintStroke` throttles the live overlay rebuild to ~60 fps
during a stroke (intermediate frames are dropped — the paint itself still lands in the
mask every tick). `endStroke` does the final exact refresh for every plane, so nothing
is lost; the screen just doesn't rebuild a multi-megapixel image faster than it can be
shown.

> Further work (deliberately not done — higher risk): partial / dirty-rect overlay
> updates that repaint only the brushed region instead of the whole slice image. The
> throttle is the cheap, safe 80% of the win.

---

## 5. The multiplier nobody sees: build configuration

This is the big one, and it is not in the algorithm at all.

A plain `swift run LumenSlice` is a **debug** build, and SwiftPM compiles C++ targets
at `-O0` (no optimization) in debug. The core is voxel-heavy, data-oriented C++ built
from tiny helpers — `Field::idx()`, `corner_index()`, `LabelVolume::at()`, lambdas —
called tens of millions of times per generate. At `-O0`:

- **none of those inline** — each is a real function call (args on the stack, call,
  return) *per voxel, per corner*;
- **nothing lives in registers** — every local is a stack load/store each use;
- **address math is recomputed** every call instead of hoisted/strength-reduced;
- **no vectorization or unrolling.**

Same Big-O, but the per-iteration constant explodes. Measured, same segmentation,
same code, reference scan:

| Build | snapshot | generate |
|---|---|---|
| `swift run` (debug, `-O0` core) | 1840 ms | **23,426 ms** |
| `swift run -c release` | 245 ms | 1,377 ms |
| `swift run` (debug, core forced `-O2`) | 189 ms | **1,404 ms** |

~17× on the generate phase. For a bigger segmentation than this test, debug crossed a
minute — which is what kicked off this whole investigation.

**Fix:** `Package.swift` forces `-O2` on the `LumenCore` target unconditionally
(`cxxSettings` → `unsafeFlags`). Clang takes the **last** `-O` flag, so:

- in **debug**, SwiftPM passes `-O0` then our `-O2` → effectively `-O2` (the win);
- in **release**, SwiftPM passes `-O2` then our `-O2` → still `-O2` (no change).

Optimizing the C++ core in debug is safe: it is pure number-crunching with no Swift to
step through, and `SegTest` runs against it. The packaged `.app` (`tools/make_app.sh`)
already builds `-c release`, so shipped builds were never affected — only dev
iteration was.

### Why `-O2` and not `-O3` for release

Measured (release, reference scan): `-O2` generate ≈ 1377 ms; `-O3` ≈ 1198–1416 ms —
**within run-to-run noise.** No real gain, because **this work is memory-bound, not
instruction-bound.** Time goes into streaming hundreds of MB of voxels and the
random-access hash-map vertex welding; `-O3`'s extra inlining/unrolling/vectorization
optimizes instruction throughput, which isn't the bottleneck — you can't unroll your
way out of waiting on RAM. `-O2` already enables clang's loop vectorizer, and `-O3`
adds code bloat (more I-cache pressure) and occasional regressions. `-O2` is also what
SwiftPM's own release config uses. So: `-O2` everywhere.

**Never `-Ofast`:** it implies `-ffast-math`, which relaxes IEEE float semantics. HU
values, spacing, and marching-cubes edge interpolation are all float math — fast-math
could subtly shift geometry. Not worth it.

---

## 6. Rules of thumb for the next perf change

- **Touch fewer voxels before optimizing the loop.** The bbox crop beat any
  micro-optimization of marching cubes because it deleted the work entirely.
- **Count in one pass.** Anything that scans the mask "per segment" should be a single
  histogram pass instead.
- **The overlay cost scales with slice size, and coronal/sagittal slices scale with
  file count.** Don't assume "it's just one slice" is cheap on a big scan.
- **Always measure debug vs release before believing a number.** A `swift run`
  timing is a `-O0`-core timing unless you've accounted for it. Use
  `swift run -c release MeshShot <folder> <out.stl> [loHU] [hiHU]` — it prints the
  snapshot and generate timings and exercises the exact bridge calls the app makes.
- **Profile the assumption that it's compute.** Here it was memory bandwidth, which is
  why `-O3` did nothing. Know which one you're bound by before reaching for flags.
```
