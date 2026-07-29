# Windows parity contract

LumenSlice has one shared C++ engine and two native presentation layers:

```text
macOS SwiftUI  ─┐
                ├─ C bridge ─ shared C++ volume/segmentation/mesh engine
Windows Qt      ─┘
```

The C++ core is shared by `Package.swift` on macOS and `CMakeLists.txt` on
Windows. The UI implementations are not interchangeable: input dispatch,
thread affinity, texture/image ownership, GPU context lifetime, menus, and
packaging are platform-specific. Every feature phase must therefore include a
Mac behavior check and a Windows wiring/build check.

## Current parity matrix

| Capability | Shared core/API | macOS UI | Windows UI | Status |
|---|---:|---:|---:|---|
| DICOM folder loading | Yes | Yes | Yes | baseline |
| Axial/coronal/sagittal views | Yes | Yes | Yes | baseline |
| 2×2 slice + 3D workspace | N/A | Yes | Yes | baseline |
| Window/level | Yes | Yes | Yes | interaction parity needed |
| Linked crosshair | Yes | Yes | Yes | modifier/hover parity needed |
| Zoom/pan/scroll input | N/A | Yes | Partial | normalize interactions |
| Multi-segment model | Yes | Yes | Yes | baseline |
| Paint and eraser | Yes | Yes | Yes | performance parity needed |
| Region grow | Yes | Yes | Yes | baseline |
| Level tracing | Yes | Yes | Yes | baseline |
| Threshold/Otsu | Yes | Yes | Yes | baseline |
| Margin grow/shrink | Yes | Yes | Yes | baseline |
| Smoothing | Yes | Yes | Yes | baseline |
| Grow from seeds | Yes | Yes | Yes | baseline |
| Island cleanup | Yes | Yes | No UI wiring | next UI phase |
| Hollow | No | No | No | shared-core phase |
| Undo/redo | Yes | Yes | Yes | baseline |
| 3D surface generation | Yes | Yes | Yes | background-job audit needed |
| 3D volume rendering | No | No | No | rendering phase |
| 3D scissor | Yes | Yes | Yes | baseline |
| STL export | Yes | Yes | Yes | baseline |
| Markups | UI-owned | Yes | Yes | persistence/parity decision pending |

## Windows requirements for every phase

1. The shared C++ tests must build and run through CMake/CTest.
2. The Windows UI must use the same bridge operation and parameter semantics as
   the Mac UI; no duplicated segmentation algorithms are allowed.
3. Long-running work must leave the Qt GUI thread responsive and must support
   safe volume replacement/cancellation.
4. Slice and overlay buffers must have explicit ownership and pixel-format
   conversions; no bridge scratch pointer may outlive the call that produced it.
5. Input behavior must be documented for mouse, wheel, trackpad/high-resolution
   wheel, modifier keys, and keyboard shortcuts.
6. Release CI must build the executable, run headless shared tests, and package
   Qt, DCMTK, the DICOM dictionary, and all required runtime plugins.

## Planned Windows work

- Wire island cleanup controls to the existing shared bridge API.
- Add a shared effect-parameter contract so Mac and Windows cannot silently
  diverge in defaults or undo boundaries.
- Move Windows slice extraction/compositing behind a cache or background render
  queue, with repaint coalescing at display rate.
- Add Qt-native keyboard/high-resolution wheel handling matching the Mac input
  semantics.
- Add CMake/CTest coverage for the same core tests used by SwiftPM.
- Add a Windows benchmark invocation to CI once a representative DICOM fixture
  is available.
