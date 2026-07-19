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

### Windows build

The Windows build uses CMake, Ninja, and DCMTK from vcpkg. On a Windows machine
with Visual Studio and vcpkg installed:

```powershell
vcpkg install --triplet x64-windows
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

The executable is `build/LumenSlice.exe`. The Windows CI workflow produces a
zip containing it and the DCMTK dictionary beside `resources/dicom.dic`.

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
