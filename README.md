# LumenSlice

A high-performance, ultra-lightweight, cross-platform C++ application designed to parse raw DICOM file paths, execute rapid real-time 2D multi-axis segmentation masks, and generate production-ready 3D-printable STL geometries.

Built with a minimalist game-engine philosophy, LumenSlice eliminates the heavy footprint of legacy medical tools, compiling natively into a single binary in under two minutes.

## Core Features

- **Zero-Bloat Architecture:** Powered by Sokol and Dear ImGui — no heavy Qt or VTK/ITK framework overhead.
- **Contiguous 3D Voxel Buffering:** Volumetric data tracks through tightly packed linear memory for rapid computing passes.
- **Real-time Tri-Axis Views:** Instantly inspect, scroll, and contrast-adjust Axial, Coronal, and Sagittal slice dimensions using highly responsive texture pipelines.
- **Multi-Threaded Segmentation:** Harness multi-threaded CPU routines for fast threshold calculations, interactive 3D paint tools, and seed-growing operations.
- **Instant Marching Cubes Extraction:** Extract and preview clean surface meshes inside a hardware-accelerated 3D viewer.
- **Native Binary STL Export:** Direct output of compliant 3D assets optimized for surgical visualization models and 3D printing pipelines.

## Technology Stack

| Concern                        | Library                                                                  |
| ------------------------------ | ------------------------------------------------------------------------ |
| Graphics context handling      | `sokol_app.h` / `sokol_gfx.h` (Metal, DirectX11/12, OpenGL 3.3+, Vulkan) |
| Immediate GUI controls         | Dear ImGui (`sokol_imgui.h`)                                             |
| DICOM input operations         | Minimized DCMTK parsing core (`dcmdata`)                                 |
| Linear transformation routines | Eigen (header-only math engine)                                          |
| Session cache systems          | SQLite (serverless, file-based)                                          |

## Documentation

**New to the codebase? Start here:** [`docs/engineering/`](docs/engineering/) is a
newcomer-friendly guide to how LumenSlice is built.

- [`docs/engineering/ARCHITECTURE.md`](docs/engineering/ARCHITECTURE.md) — the three layers, data flow, module map, and a "where do I fix X?" table
- [`docs/engineering/CPP_ONBOARDING.md`](docs/engineering/CPP_ONBOARDING.md) — the C++ idioms used, explained for non-C++ developers
- [`docs/engineering/DESIGN_PATTERNS.md`](docs/engineering/DESIGN_PATTERNS.md) — the design patterns in the code (Facade, Strategy, Command/Memento, ...) and how to add a new segmentation tool

Reference / planning docs:

- [`docs/context.md`](docs/context.md) — deep technical architecture specification
- [`docs/dependencies.md`](docs/dependencies.md) — open-source libraries, where each is used, and licenses
- [`docs/plan.md`](docs/plan.md) — implementation blueprint and requirements
- [`docs/agent.md`](docs/agent.md) — AI code-generation directives
- [`docs/timelines.md`](docs/timelines.md) — 8-week release schedule

## Quick Start Development Guide

### 1. Prerequisites

Phase 1 (macOS) needs the Xcode command-line tools (Swift 5.9+) and DCMTK:

```bash
# macOS (via Homebrew)
xcode-select --install   # if not already installed
brew install dcmtk
```

> The future Windows/Linux cross-platform targets will use the C++/CMake/Ninja
> toolchain described in [`docs/timelines.md`](docs/timelines.md). Windows now
> has a native Win32 shell that reuses the same C++ core; the richer SwiftUI
> shell remains the macOS front-end.

### 2. Build & Run (Phase 1 — macOS, SwiftUI shell)

Phase 1 ships a native **SwiftUI** macOS app driven by Swift Package Manager. The
C++ data core (DICOM ingestion, slice extraction) is reused unchanged behind a
small C bridge — see [Architecture note](#architecture-note-swiftui-shell).

```bash
git clone --recursive https://github.com/yourusername/LumenSlice.git
cd LumenSlice

# Build the C++ core + Swift app
swift build

# Generate a synthetic CT phantom series to test ingestion (optional)
python3 -m pip install --user pydicom numpy
python3 tools/make_test_series.py testdata/phantom

# Launch the viewer; pass a folder to auto-load, or drag a DICOM folder onto the window
swift run LumenSlice testdata/phantom

# Headless ingestion check (no window) — exercises the same C bridge
swift run IngestTest testdata/phantom
```

#### Try it on a real scan

The loader reads **uncompressed** DICOM. A clean, directly-downloadable example is
the datalad T1-weighted MRI (384 single-frame slices, uncompressed):

```bash
curl -sL -o /tmp/t1.zip \
  https://codeload.github.com/datalad/example-dicom-structural/zip/refs/heads/master
unzip -q /tmp/t1.zip -d /tmp/t1
cp /tmp/t1/example-dicom-structural-master/dicoms/*.dcm testdata/t1_mri/   # mkdir first

swift run LumenSlice testdata/t1_mri        # open in the app
swift run SliceShot testdata/t1_mri out.png # or render the 3 centre slices to a PNG
```

### Windows build (Qt 6 GUI)

The Windows front-end is a native **Qt 6 Widgets** application (`gui/win/`) at
feature parity with the macOS SwiftUI app: an icon rail (Visualize / Segment / 3D
/ Export), the tri-axis slice board (Axial / Coronal / Sagittal) with wheel-scroll,
window/level drag, crosshair linking and a colored mask overlay, the full
multi-segment editing suite (threshold, region-grow, level-trace, paint/erase,
refine, grow-from-seeds, undo/redo), a `QOpenGLWidget` 3D mesh viewer, and STL/PNG
export. It reuses the same UI-agnostic C++ core through the pure-C
[`lumen_bridge.h`](src/bridge/include/lumen_bridge.h) — nothing under `src/` is
Windows-specific. It replaces the earlier minimal Win32 shell
(`windows/LumenSliceWin.cpp`), which stays in the tree for reference but is no
longer built.

**Dependencies:** CMake + Ninja + MSVC, **DCMTK/zlib from vcpkg**, and **Qt 6 as
prebuilt binaries** (Widgets / OpenGLWidgets / Concurrent, all in `qtbase`).
Building Qt from vcpkg source is impractical, so Qt is installed prebuilt — with
[`aqtinstall`](https://github.com/miurahr/aqtinstall) locally and
[`install-qt-action`](https://github.com/jurplel/install-qt-action) in CI.

```powershell
# 1. DCMTK + zlib (release-only overlay triplet halves the DCMTK build)
vcpkg install --triplet x64-windows-rel --overlay-triplets=triplets

# 2. Prebuilt Qt 6 (once)
pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.1 win64_msvc2022_64 --outputdir C:\Qt

# 3. Configure + build (from a Developer prompt so cl.exe/CMake/Ninja are on PATH)
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-rel `
  -DVCPKG_MANIFEST_MODE=OFF `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.1/msvc2022_64"
cmake --build build --config Release

# 4. Bundle the Qt runtime beside the exe, then run
& C:\Qt\6.8.1\msvc2022_64\bin\windeployqt.exe --release build\LumenSlice.exe
build\LumenSlice.exe                 # or: build\LumenSlice.exe path\to\dicom_folder
```

The executable is `build\LumenSlice.exe`. The Windows CI workflow
([`.github/workflows/windows.yml`](.github/workflows/windows.yml)) installs Qt with
`install-qt-action`, builds, runs `windeployqt`, and produces a zip containing the
exe, the Qt + DCMTK runtime, and the DICOM dictionary at `resources/dicom.dic`.

**Deferred / notes.** *Markups* are out of scope for v1: they have no
`lumen_bridge.h` functions (they live entirely in the macOS Swift layer), and the
UI/data isolation rule forbids adding core code for them from the UI side. The 3D
*scissor* lasso (`lumen_seg_scissor_cut`) is likewise deferred — the bridge
supports it, but wiring the screen-space MVP + lasso capture in the OpenGL viewer
is follow-up work. If segment *names* should persist in the core (they are
currently a Windows-UI-only convenience), the smallest bridge additions would be
`void lumen_seg_set_name(LumenVolume*, int id, const char* utf8)` and
`int lumen_seg_get_name(const LumenVolume*, int id, char* out, int out_cap)`.

`SliceShot` renders the axial/coronal/sagittal centre slices through the same
bridge + window/level + CGImage path as the app — handy for a quick headless
preview or CI snapshot. For more datasets (and how to decompress JPEG/JPEG2000
series with `dcmdjpeg`/`gdcmconv`), see the dataset notes discussed in the project
chat / `docs`.

#### Run from Xcode

The package opens directly in Xcode — no `.xcodeproj` needed:

```bash
open Package.swift     # or: xed .
```

Then in Xcode:

1. Wait for package resolution to finish.
2. **In the scheme selector (top toolbar), choose `LumenSlice` — _not_ `LumenSlice-Package`.**
   The auto-generated `…-Package` scheme builds every target but launches nothing,
   so running it shows no window. `IngestTest` is a headless console target with no
   window either — pick `LumenSlice`.
3. Set the destination to **My Mac**.
4. Press **⌘R** to build and run.
5. _(Optional)_ To auto-load a folder on launch: **Product ▸ Scheme ▸ Edit Scheme… ▸ Run ▸ Arguments**, and add the folder path. Otherwise use **Open Folder** or drag a DICOM folder onto the window.

> DCMTK is linked from Homebrew with absolute library paths, so the app finds it
> at runtime under Xcode without any extra `DYLD`/rpath configuration.

> **Status — Phase 1 (Week 1–2 milestone) complete on macOS.** DCMTK-based DICOM
> ingestion (recursive folder crawl, `DICM` signature filter, HU rescale,
> geometric Z-sort by Image Position/Orientation), a flat contiguous voxel core,
> and a native tri-axis slice viewer (Axial/Coronal/Sagittal) with live
> window/level, presets, physical-aspect rendering, a folder picker, and
> drag-and-drop. Segmentation, marching cubes, and STL export follow the
> [8-week plan](docs/timelines.md).

### Share the app with someone (.dmg)

```bash
ADHOC=1 tools/make_app.sh
```

This builds a release, wraps it in `dist/LumenSlice.app`, ad-hoc signs it, and
packages a drag-to-`/Applications` `dist/LumenSlice.dmg`. DCMTK is linked
**statically** and its data dictionary is bundled in `Contents/Resources`, so the
app has **no Homebrew or DCMTK dependency** — it runs on a clean Mac. No Apple
Developer account is required.

Send `dist/LumenSlice.dmg`. Because it is not notarized, the recipient clears
Gatekeeper **once** on first launch — right-click `LumenSlice.app` → **Open** →
**Open**, or run `xattr -dr com.apple.quarantine /Applications/LumenSlice.app`.
After that it opens normally.

**Don't want to build it yourself?** Every push to `main` runs CI
([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) which builds, tests, and
publishes the ad-hoc `.dmg` to the repository's rolling **`latest`** GitHub
Release — download it from the Releases page.

> **Optional — notarized, zero-warning .dmg.** With a *paid* Apple Developer
> membership you can produce a notarized build that opens with a plain
> double-click (no right-click dance). Run `tools/make_app.sh` (no `ADHOC=1`); the
> one-time setup is documented in
> [`tools/NOTARIZE_SETUP.md`](tools/NOTARIZE_SETUP.md).

Caveat: the bundle targets the **build host's architecture** (Apple Silicon →
arm64; an Intel Mac can't run it).

### Architecture note (SwiftUI shell)

The original blueprint specified a Sokol + Dear ImGui shell for a single
cross-platform binary. Phase 1 instead uses a **SwiftUI** front-end for a cleaner,
fully-native macOS look. Crucially, this only swaps the _presentation layer_: the
data-oriented C++ core (`src/core`, `src/io`, `src/visualization`) is untouched
and stays UI-agnostic per [`docs/agent.md`](docs/agent.md) §1, exposed to Swift
through a thin C API (`src/bridge`). The Sokol/ImGui path can be revived for the
Windows/Linux targets in the weeks 7–8 cross-platform work without changing the
core. (`docs/context.md` and the stack table above still describe the original
Sokol plan and will be reconciled as cross-platform work resumes.)

## License

LumenSlice is distributed under the open-source [MIT License](LICENSE). Contributions and performance patches from graphics purists and medical imaging innovators are highly encouraged.
