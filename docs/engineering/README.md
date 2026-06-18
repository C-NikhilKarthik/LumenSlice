# LumenSlice engineering docs

Start here if you are new to this codebase (or to C++). These docs are written to
get you from "I cloned the repo" to "I can find and fix the thing I care about"
quickly, without needing to read every file.

Read them in this order:

1. **[ARCHITECTURE.md](ARCHITECTURE.md)** - the big picture. The three layers, how a
   DICOM folder becomes pixels on screen and a 3D surface, the module map, and a
   "where do I look to fix X?" table. Read this first.
2. **[CPP_ONBOARDING.md](CPP_ONBOARDING.md)** - the C++ idioms this codebase uses,
   explained for someone who does not write C++ daily: RAII, smart pointers,
   headers vs. source, namespaces, `const`, and the C bridge to Swift. Each idiom
   points at the real file where you can see it.
3. **[DESIGN_PATTERNS.md](DESIGN_PATTERNS.md)** - the design patterns in the code
   (Facade, Strategy, Command/Memento, Adapter, and more), why each is here, and
   the exact class/file that implements it. Read this when you want to extend the
   app, e.g. add a new segmentation tool.

## The 30-second version

LumenSlice is a macOS medical-image viewer. It has three layers:

```
  C++ core (pixels + geometry, no UI)   ->   pure-C bridge   ->   SwiftUI app
  src/core, src/io, src/geometry,            src/bridge           app/
  src/visualization, src/segmentation
```

- The **C++ core** does all the heavy lifting on raw numbers: load DICOM, slice the
  volume, threshold/paint/grow a segmentation, run marching cubes for 3D. It knows
  nothing about windows or buttons.
- The **bridge** is a thin C API (`src/bridge/include/lumen_bridge.h`) that lets
  Swift call the C++ without touching C++ types directly.
- The **Swift app** (`app/`) is the UI: the three slice panes, the segment editor,
  the 3D view.

## Building and testing

```bash
swift build              # build everything
swift run LumenSlice testdata/phantom   # run the app on the bundled phantom
swift run SegTest        # C++ unit tests for the segmentation core (fast, headless)
swift run MeshShot testdata/phantom /tmp/out.stl 300 3000   # headless end-to-end check
```

`SegTest` is the fastest way to know the core still works after a change. It runs
in well under a second and exercises geometry, thresholding, region grow, paint,
islands, Otsu, undo, morphology, the Strategy effects, and the `SegmentEditor`.
