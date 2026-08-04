# Dependencies: Buy vs. Build

LumenSlice is "zero-bloat" by **picking the right small libraries**, not by writing
everything from scratch. The rule of thumb: we own the *data-oriented core*
(voxel buffers, masks, segmentation math, STL byte layout) and we **lean on
open source for the undifferentiated heavy lifting** (windowing, GPU backends,
DICOM parsing, linear algebra). This is how an 8-week timeline is realistic.

This document is the single source of truth for the dependency matrix. The
[README](../README.md) and [`context.md`](context.md) summarize it.

## At a glance

| Library | Role | What it saves us | Used in | Integration | License |
| --- | --- | --- | --- | --- | --- |
| **DCMTK** (`dcmdata` only) | DICOM parsing | A correct DICOM tag/VR parser, transfer-syntax handling, rescale metadata | `src/io/dicom_loader.cpp` | system package / FetchContent | BSD-3-Clause-like (DCMTK) |
| **Marching Cubes tables** | Isosurface extraction | The 256-entry edge/triangle lookup tables + traversal | `src/visualization/` (mesh gen) | vendored single-header (see below) | see note |

## Why each one

### DCMTK (`dcmdata` subset) — DICOM input

DICOM is deceptively hard: tag dictionaries, value representations, transfer
syntaxes, and rescale slope/intercept. We use **only `dcmdata`** to read pixel
data and the metadata we need — Rescale Slope/Intercept `(0028,1052/1053)`,
Pixel Spacing `(0028,0030)`, Image Orientation `(0020,0037)`, Image Position
`(0020,0032)` — then drop everything into our own flat `float` buffer. We
deliberately do **not** pull DCMTK's network or image-toolkit layers.

- Used by: `src/io/dicom_loader.cpp` (folder crawl, `"DICM"` signature check at
  byte 128, tag extraction, geometric Z-sorting).

### Marching Cubes — isosurface extraction (read the license note)

The mask volume becomes a printable mesh via Marching Cubes. The hard part is
not the loop — it's the canonical **256-entry edge and triangle lookup tables**
(Lorensen & Cline). Plan: vendor a **single-header** implementation under
the C++ mesh pipeline and STL exporter.

> **Note on `nihaljn/marching-cubes`:** it's a clean C++ reference (reads a
> scalar field, writes PLY) and is useful to study, but as of writing the repo
> **declares no license**, so we cannot copy its source into an MIT project.
> Safe paths: (a) adapt the **public-domain Lorensen/Bourke tables** directly,
> or (b) vendor a permissively licensed single-header such as a
> `MarchingCubes.h` released under MIT/zlib/public-domain. Confirm the license
> before adding a new vendored dependency.

- Used by: `src/visualization/` mesh generation, feeding `src/io/stl_exporter.cpp`.

## How dependencies are wired in

Integration uses two styles:

1. **CMake/system packages** — DCMTK and Qt are provided by the platform build
   environment; CMake links the Windows Qt target directly.
2. **CMake `FetchContent`** — convenient pinned fetch at configure time; a good
option for dependencies where no system package exists.
3. **System package** — DCMTK is large; preferring the OS package
   (`libdcmtk-dev`, `brew install dcmtk`, vcpkg) keeps our build fast.

The top-level `CMakeLists.txt` and `Package.swift` own this wiring. There are no
vendored UI or graphics submodules in the repository.

## License compatibility (we ship under MIT)

| Dependency | License | MIT-compatible? |
| --- | --- | --- |
| DCMTK | BSD-style (DCMTK) | ✅ (preserve copyright notice) |
| `nihaljn/marching-cubes` | **none declared** | ❌ do not copy — see note above |

Keep this table honest as dependencies are added: a "zero-bloat" promise is also
a promise that every byte we ship has a known, compatible license.
