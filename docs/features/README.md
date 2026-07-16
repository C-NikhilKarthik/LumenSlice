# LumenSlice feature documentation

LumenSlice is a macOS medical-image viewer. It takes a folder of DICOM files (the
raw output of a CT or MR scanner), assembles them into one contiguous Hounsfield-Unit
volume, and lets you window it, scroll through it on three axes, segment structures
by painting or growing masks, drop point/line/plane markups, and turn a segmentation
into a 3D surface you can export as STL or PNG. Under the hood it is split into three
layers with a strict downward dependency: a C++ core that does all the pixel and
number work and knows nothing about the UI, a thin pure-C bridge that lets Swift call
that core without touching C++ types, and a SwiftUI app that drives it. These docs
walk each subsystem end to end, following the data and the user's edits through those
layers rather than describing files in isolation.

## Documents

Read [ARCHITECTURE_OVERVIEW.md](ARCHITECTURE_OVERVIEW.md) first for the shape of the
whole app; the rest can be read in any order.

| Document | What it covers |
| --- | --- |
| [ARCHITECTURE_OVERVIEW.md](ARCHITECTURE_OVERVIEW.md) | The three-layer split (C++ core, C bridge, SwiftUI app) and how a scan and the user's edits flow across them. |
| [DICOM_AND_VOLUME.md](DICOM_AND_VOLUME.md) | DICOM ingest, the flat HU volume layout, and how a single slice is extracted and windowed. |
| [VIEWER_NAVIGATION.md](VIEWER_NAVIGATION.md) | The tri-axis slice viewer, every navigation gesture, and the screen-to-voxel coordinate pipeline. |
| [SEGMENTATION.md](SEGMENTATION.md) | All segmentation tools plus the Facade, Strategy, and Memento patterns that drive editing and undo. |
| [THREED_AND_EXPORT.md](THREED_AND_EXPORT.md) | Marching cubes, the SceneKit view, and STL/PNG export. |
| [MARKUPS.md](MARKUPS.md) | Point, line, and plane fiducials, placed in 2D and echoed in the 3D view. |

## Diagrams

The SVGs under [diagrams/](diagrams/) are embedded by the docs above; each is also
readable on its own.

- [architecture-layers.svg](diagrams/architecture-layers.svg) - the three layers and their downward dependency.
- [data-flow.svg](diagrams/data-flow.svg) - a scan and its edits travelling through the layers.
- [dicom-ingest.svg](diagrams/dicom-ingest.svg) - the DICOM folder to HU volume pipeline.
- [coordinate-pipeline.svg](diagrams/coordinate-pipeline.svg) - screen point to voxel via the fitted rect.
- [input-event-routing.svg](diagrams/input-event-routing.svg) - the NSEvent monitors and what each consumes or passes through.
- [segmentation-classes.svg](diagrams/segmentation-classes.svg) - the Strategy effect hierarchy for segmentation tools.
- [undo-memento.svg](diagrams/undo-memento.svg) - the snapshot-based undo stack.
- [mesh-pipeline.svg](diagrams/mesh-pipeline.svg) - mask to mesh to STL export.
- [markups-flow.svg](diagrams/markups-flow.svg) - placing a markup and rendering it in 2D and 3D.

## Going deeper

These feature docs follow behaviour end to end. For the internals of each piece in
isolation - the module map, the C++ idioms, the pattern catalogue, and the
performance hot paths - see the engineering docs under
[docs/engineering/](../engineering/README.md).
