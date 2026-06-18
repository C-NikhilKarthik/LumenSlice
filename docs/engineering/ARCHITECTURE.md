# LumenSlice architecture

This is the map of the codebase. Once you understand the three layers and the data
flow, every file has an obvious home.

## 1. The three layers

LumenSlice is split into three layers with a strict dependency direction. Each
layer only knows about the one below it.

```
  +-------------------------------------------------------------+
  |  SwiftUI app                         app/                    |   what the user sees
  |  windows, panes, gestures, 3D view, models (VolumeModel...)  |
  +-------------------------------------------------------------+
                         |  calls C functions
                         v
  +-------------------------------------------------------------+
  |  Pure-C bridge                       src/bridge/             |   the language seam
  |  one opaque handle + plain C functions (lumen_bridge.h)      |
  +-------------------------------------------------------------+
                         |  calls C++
                         v
  +-------------------------------------------------------------+
  |  C++ core (no UI, just numbers)      src/core, src/io,       |   the real work
  |  src/geometry, src/visualization, src/segmentation          |
  +-------------------------------------------------------------+
```

Why split this way? The rule (see `docs/agent.md`) is that pixel/number code never
imports UI code, and UI code never reaches into C++ types. The bridge is the only
place the two worlds meet, and it speaks plain C so Swift can call it safely.

### Where each layer lives

| Layer | Folder | Responsibility |
|---|---|---|
| C++ core: data model | `src/core/` | `Volume` - the flat HU voxel buffer (`volume.h`). |
| C++ core: ingestion | `src/io/` | Read DICOM folders into a `Volume`; parse metadata. |
| C++ core: geometry | `src/geometry/` | `plane_map` - the ONE place slice-pixel <-> voxel mapping lives. |
| C++ core: viewing | `src/visualization/` | `ExtractSlice` - turn a plane of HU into a windowed grayscale image. |
| C++ core: segmentation | `src/segmentation/` | The mask, segments, effects, undo, marching cubes, STL. |
| Bridge | `src/bridge/` | The C API + the opaque handle struct. |
| Swift app | `app/` | UI, gestures, the observable models. |

## 2. Data flow: from DICOM folder to screen

```
  DICOM folder
      |  LoadDicomFolder()                         src/io/dicom_loader.cpp
      v
  Volume  (flat float[] of Hounsfield Units)       src/core/volume.h
      |  ExtractSlice(axis, index, level, window)  src/visualization/slice_view.cpp
      v
  RGBA8 slice buffer  -- via the bridge -->  CGImage  -->  SlicePane on screen
                         lumen_extract_slice        app/VolumeModel.swift / SlicePane.swift
```

The `Volume` is one contiguous `float[]` of HU values, X fastest then Y then Z (see
`Volume::index`). There is no per-slice object; a "slice" is computed on demand by
walking that buffer. `plane_map` (`src/geometry/`) is the single source of truth for
how a 2D pane pixel maps to a 3D voxel on each axis, including the coronal/sagittal
flip. If a slice ever looks mirrored or transposed, that one file is where you look.

## 3. Data flow: segmentation and 3D

The segmentation lives behind one class, `SegmentEditor` (`src/segmentation/
segment_editor.hpp`), which the bridge holds inside the handle.

```
  SegmentEditor                                    src/segmentation/segment_editor.*
   |  owns:
   |    LabelVolume mask_     (1 byte/voxel: 0 = bg, 1..255 = segment id)
   |    SegmentTable segments_(colour + visibility + which id is active)
   |    UndoStack   undo_     (bounded, RLE-compressed snapshots)
   |
   |  edits run as effects (Strategy pattern)      src/segmentation/effects.*
   |    ThresholdEffect, PaintEffect, RegionGrowEffect, GrowMarginEffect, ...
   |    each delegates to a pure kernel in         src/segmentation/segment.cpp
   |                                               src/segmentation/analysis.cpp
   v
  mask -> ExtractMaskSlice  -> coloured overlay RGBA  (src/segmentation/mask_view.cpp)
  mask -> marching_cubes    -> triangle Mesh -> STL / SceneKit  (marching_cubes.cpp)
```

The 3D surface is built per visible segment: the mask is binarized for one segment
id, snapshotted, and marched into a triangle mesh. Generation is split into a
main-thread snapshot and a background-thread march so it never races the live mask
the user is still painting (see `lumen_mesh_snapshot` / `lumen_mesh_generate`).

## 4. The Swift side (app/)

The app follows MVVM. Three `ObservableObject` models drive the same C++ handle:

| Model | File | Owns |
|---|---|---|
| `VolumeModel` | `app/VolumeModel.swift` | The loaded volume, the three slice images, the shared crosshair focus, window/level, overlay toggles. |
| `SegmentationModel` | `app/Model/SegmentationModel.swift` | The segment list, active segment, tools, brush, undo state, and the mask overlays. |
| `MeshModel` | `app/ThreeD/MeshModel.swift` | The per-segment 3D surfaces. |

The UI is split by tab: `app/Tabs/` has one controls file per tab, `app/Viewer/` has
the slice panes and overlays, `app/ThreeD/` has the SceneKit view.

## 5. Where do I look to fix X?

| Symptom / task | Start here |
|---|---|
| A slice looks flipped, mirrored, or transposed | `src/geometry/plane_map.cpp` (the one mapping) |
| Brightness/contrast (window/level) is wrong | `src/visualization/slice_view.cpp`, then `VolumeModel.refresh` |
| A segmentation operation produces the wrong mask | The effect in `src/segmentation/effects.cpp`, then its kernel in `segment.cpp` / `analysis.cpp` |
| Add a brand-new segmentation tool | See DESIGN_PATTERNS.md "Adding a new effect" - one new class, no edits to the editor |
| Undo/redo is wrong | `src/segmentation/undo_stack.cpp` + the capture points in `SegmentationModel.swift` |
| The coloured overlay is the wrong colour or hidden | `src/segmentation/mask_view.cpp` + `SegmentTable` |
| The 3D surface is wrong or missing | `src/segmentation/marching_cubes.cpp`, then `MeshModel.swift` |
| DICOM fails to load / parse | `src/io/dicom_loader.cpp`, `src/io/dicom_meta.cpp` |
| A C function Swift calls is missing/odd | `src/bridge/include/lumen_bridge.h` + the matching `lumen_bridge_*.cpp` |
| UI layout, gestures, tabs | `app/` (`Shell/`, `Tabs/`, `Viewer/`, `ThreeD/`) |

## 6. The bridge in one paragraph

Swift cannot call C++ directly, so `src/bridge/` exposes a plain-C API. Swift holds
an opaque `LumenVolume*` (it never sees the real struct). Every C function takes
that handle and forwards to C++. The handle struct itself is defined in a private
header, `src/bridge/lumen_handle.hpp`, shared by the three bridge translation units
(`lumen_bridge_volume.cpp`, `lumen_bridge_segment.cpp`, `lumen_bridge_mesh.cpp`) -
one file per concern. The public C surface is `src/bridge/include/lumen_bridge.h`;
read it top to bottom and you have the entire contract between Swift and C++.
